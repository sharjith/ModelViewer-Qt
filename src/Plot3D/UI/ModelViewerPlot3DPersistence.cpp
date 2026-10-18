// Saving and reopening 3D Plot sessions in .mvf files (the ModelViewer half of Plot3DSessionIO.h).
//
// A plot's MESH has always round-tripped as ordinary scene content. What did not survive a save was everything around
// it: the Plot3DSession (axes, titles, colour range/map/bands, line/marker/arrow/bar sizing, reference planes, the
// editable CSV), the colours themselves (they are a transient analysis overlay, not authored vertex data), and - for
// Quiver and Voxel - the renderer data that lives in a viewport controller rather than in any mesh. Saving stores the
// session as JSON in mvfSession with its bulk arrays as compressed blobs in the GEOM chunk, exactly the way Simulation
// snapshots are stored; loading puts the session back and re-derives every colour and renderer state through the same
// applyPlot3DColourState() / applyPlot3DAppearance() paths a user edit goes through.

#include "ModelViewer.h"

#include "MvfSceneBuilder.h"
#include "Plot3DSessionIO.h"
#include "Plot3DVoxelStyle.h"
#include "SceneMesh.h"
#include "SimulationGlyphs.h"
#include "SimulationVolume.h"
#include "ViewportWidget.h"

#include <QDebug>
#include <QJsonArray>

#include <algorithm>

namespace
{
	void padTo4(QByteArray& buffer)
	{
		while (buffer.size() % 4 != 0)
			buffer.append(char(0));
	}
}

void ModelViewer::appendPlot3DSessions(Mvf::MVFPackage& package) const
{
	if (_plot3DSessions.isEmpty() || !_viewportWidget)
		return;

	QJsonArray plots;
	// plot3DSessions() is the live, presentation-consistent list: a plot whose mesh sits in the recycle bin awaiting
	// Undo is not in it, and is not in the saved scene either.
	for (const Plot3DSession& session : plot3DSessions())
	{
		Plot3DRendererPayload payload;
		if (session.primitive == Plot3DPrimitive::Quiver)
		{
			if (const GlyphSet* glyphs = _viewportWidget->simulationGlyphSet(session.meshUuid))
			{
				payload.hasGlyphs = true;
				payload.glyphVectors = glyphs->vectors;
				payload.glyphValues = glyphs->values;
				payload.glyphReferenceLength = glyphs->referenceLength;
				payload.glyphFieldMinimum = glyphs->fieldMin;
				payload.glyphFieldMaximum = glyphs->fieldMax;
			}
		}
		else if (session.primitive == Plot3DPrimitive::Voxel)
		{
			if (const VolumeGrid* grid = _viewportWidget->simulationVolumeGrid(session.meshUuid))
			{
				payload.hasVolume = true;
				payload.volumeValues = grid->values;
				payload.volumeDimensions[0] = grid->dimX;
				payload.volumeDimensions[1] = grid->dimY;
				payload.volumeDimensions[2] = grid->dimZ;
				for (int axis = 0; axis < 3; ++axis)
				{
					payload.volumeOrigin[axis] = grid->origin[axis];
					payload.volumeVoxelSize[axis] = grid->voxelSize[axis];
				}
				payload.volumeFieldMinimum = grid->fieldMin;
				payload.volumeFieldMaximum = grid->fieldMax;
				payload.volumeLabel = grid->label;
			}
		}

		// A Quiver or Voxel plot IS its renderer data; its mesh is only the anchor / bounds proxy. A session saved without
		// it would reopen "successfully" as an empty plot, so skip it (the mesh still saves as scene content) and say so.
		if ((session.primitive == Plot3DPrimitive::Quiver && !payload.hasGlyphs)
			|| (session.primitive == Plot3DPrimitive::Voxel && !payload.hasVolume))
		{
			qWarning() << "3D Plot session not saved: no renderer data for" << session.name;
			_simulationSaveNotes << tr("The 3D plot '%1' was saved without its editable plot data because its renderer data was unavailable.")
				.arg(session.name);
			continue;
		}

		std::vector<QByteArray> blobs;
		QJsonObject entry = plot3DSessionToJson(session, payload, blobs);

		// The blobs go into the GEOM buffer, each as its own bufferView (glTF-style indirection, like Simulation snapshots).
		QJsonArray views;
		for (std::size_t i = 0; i < blobs.size(); ++i)
		{
			padTo4(package.geometryChunk);
			const qint64 offset = package.geometryChunk.size();
			package.geometryChunk.append(blobs[i]);
			QJsonObject view;
			view.insert(QStringLiteral("buffer"), 0);
			view.insert(QStringLiteral("byteOffset"), offset);
			view.insert(QStringLiteral("byteLength"), static_cast<qint64>(blobs[i].size()));
			view.insert(QStringLiteral("name"), QStringLiteral("PLOT3D_%1_%2").arg(session.meshUuid.toString(QUuid::WithoutBraces)).arg(i));
			views.append(package.document.bufferViews.size());
			package.document.bufferViews.append(view);
		}
		entry.insert(QStringLiteral("blobViews"), views);
		plots.append(entry);
	}
	if (plots.isEmpty())
		return;
	package.document.mvfSession.insert(QStringLiteral("plot3dPlots"), plots);
	package.document.mvfSession.insert(QStringLiteral("plot3dActiveMesh"), activePlot3DMeshUuid().toString(QUuid::WithoutBraces));
}

void ModelViewer::restorePlot3DSessions(QVector<PendingPlot3DRestore>& restores, const QUuid& activeMesh)
{
	if (restores.isEmpty() || !_viewportWidget)
		return;

	// Restoring drives the same apply* paths a user edit does, and those mark the document modified. A document that was
	// just opened is not modified, so put the flags back exactly as the load left them.
	const bool savedFlag = _documentSaved;
	const bool dirtyFlag = _nonUndoDocumentDirty;
	const bool modifiedFlag = _documentModified;

	QVector<QUuid> restored;
	for (PendingPlot3DRestore& pending : restores)
	{
		Plot3DSession& session = pending.session;
		SceneMesh* mesh = _viewportWidget->getMeshByUuid(session.meshUuid);
		if (!mesh)
			continue; // the plot's mesh is not in the file any more; there is nothing to attach the session to

		// Every per-vertex array is uploaded to the GPU as an attribute of this mesh, so a count that disagrees with the
		// restored mesh (a damaged or hand-edited file) would hand the driver an undersized buffer. Drop the session
		// instead; the mesh itself still stands as ordinary scene content.
		const std::size_t vertexCount = mesh->vertices().size();
		const SceneMesh* stemMarkerMesh = session.markerMeshUuid.isNull() ? nullptr : _viewportWidget->getMeshByUuid(session.markerMeshUuid);
		const bool colourMismatch = (!session.values.empty() && session.values.size() != vertexCount)
			|| (!session.markerValues.empty() && (!stemMarkerMesh || session.markerValues.size() != stemMarkerMesh->vertices().size()))
			|| (pending.payload.hasGlyphs && pending.payload.glyphValues.size() != vertexCount);
		if (colourMismatch)
		{
			qWarning() << "3D Plot session not restored: its colour data does not match its mesh -" << session.name;
			continue;
		}

		// Renderer data that lives outside the mesh.
		const Plot3DRendererPayload& payload = pending.payload;
		if (payload.hasGlyphs)
		{
			// One arrow per site, anchored to the plot's own point mesh: vertex i is arrow i (all three anchor slots the
			// same vertex - GlyphSet's "a node arrow repeats one vertex" convention). Colours are re-derived below.
			const std::size_t count = payload.glyphValues.size();
			GlyphSet glyphs;
			glyphs.anchors.resize(count * 3);
			for (std::size_t i = 0; i < count; ++i)
				glyphs.anchors[i * 3] = glyphs.anchors[i * 3 + 1] = glyphs.anchors[i * 3 + 2] = static_cast<std::uint32_t>(i);
			glyphs.vectors = payload.glyphVectors;
			glyphs.values = payload.glyphValues;
			glyphs.colors.assign(count * 3, 1.0f);
			glyphs.fieldMin = payload.glyphFieldMinimum;
			glyphs.fieldMax = payload.glyphFieldMaximum;
			glyphs.referenceLength = payload.glyphReferenceLength;
			_viewportWidget->setSimulationGlyphs(session.meshUuid, std::move(glyphs));
			_viewportWidget->setSimulationGlyphScale(session.meshUuid, session.arrowScale);
		}
		if (payload.hasVolume)
		{
			VolumeGrid grid;
			grid.values = payload.volumeValues;
			grid.dimX = payload.volumeDimensions[0];
			grid.dimY = payload.volumeDimensions[1];
			grid.dimZ = payload.volumeDimensions[2];
			for (int axis = 0; axis < 3; ++axis)
			{
				grid.origin[axis] = payload.volumeOrigin[axis];
				grid.voxelSize[axis] = payload.volumeVoxelSize[axis];
			}
			grid.fieldMin = payload.volumeFieldMinimum;
			grid.fieldMax = payload.volumeFieldMaximum;
			grid.label = payload.volumeLabel;
			if (!grid.empty())
				_viewportWidget->setSimulationVolume(session.meshUuid, std::move(grid), session.colormap, plot3DVoxelOpacity());
		}

		// A contour overlay is a companion mesh stored with the plot. Without it in the file the setting means nothing.
		if (session.contourOverlayMode != 0 && (session.contourOverlayMeshUuid.isNull()
			|| !_viewportWidget->getMeshByUuid(session.contourOverlayMeshUuid)))
		{
			session.contourOverlayMode = 0;
			session.contourOverlayMeshUuid = QUuid();
		}

		// Per-mesh appearance (a native line/point's pixel size is a property of the mesh, not stored with it).
		mesh->setPrimitiveLineWidth(session.lineWidth);
		if (!session.contourOverlayMeshUuid.isNull())
			if (SceneMesh* overlayMesh = _viewportWidget->getMeshByUuid(session.contourOverlayMeshUuid))
				overlayMesh->setPrimitiveLineWidth(session.lineWidth);
		mesh->setPrimitivePointSize(session.markerSize);
		if (!session.markerMeshUuid.isNull())
			if (SceneMesh* markerMesh = _viewportWidget->getMeshByUuid(session.markerMeshUuid))
				markerMesh->setPrimitivePointSize(session.markerSize);

		// A session this document already holds for the same mesh (reopening over itself) is replaced, not duplicated.
		for (int i = _plot3DSessions.size() - 1; i >= 0; --i)
			if (_plot3DSessions[i].meshUuid == session.meshUuid)
				_plot3DSessions.removeAt(i);
		_plot3DSessions.push_back(session);
		restored.push_back(session.meshUuid);
	}

	// Colours: every session is in the list first, so the shared legend and axes see the whole set. The GL context is
	// made current per plot because the recolour paths upload buffers (and one of them releases the context itself).
	for (const QUuid& meshUuid : std::as_const(restored))
	{
		const Plot3DSession* stored = nullptr;
		for (const Plot3DSession& candidate : std::as_const(_plot3DSessions))
			if (candidate.meshUuid == meshUuid)
				stored = &candidate;
		if (!stored)
			continue;
		// The overlay's lines are re-derived from the restored surface (its colour values are not saved). This runs before
		// the context is made current below because the rebuild manages the context itself.
		if (stored->contourOverlayMode != 0)
			refreshPlot3DContourOverlay(meshUuid);
		// Copied: applyPlot3DColourState() writes the same values back into the stored session.
		const float minimum = stored->colourMinimum, maximum = stored->colourMaximum;
		const int colormap = stored->colormap, bands = stored->bands;
		_viewportWidget->makeCurrent();
		applyPlot3DColourState(meshUuid, minimum, maximum, colormap, bands);
		_viewportWidget->doneCurrent();
	}

	_activePlot3DMesh = restored.contains(activeMesh) ? activeMesh : (restored.isEmpty() ? QUuid() : restored.front());
	if (!_activePlot3DMesh.isNull())
	{
		unifyPlot3DAxes(_activePlot3DMesh); // a file saved with different axes per plot (older versions) opens on the active plot's, shared
		activatePlot3DSession(_activePlot3DMesh); // axes box, legend and the 3D Plot tab
	}
	_viewportWidget->updateView();

	_documentSaved = savedFlag;
	_nonUndoDocumentDirty = dirtyFlag;
	setDocumentModified(modifiedFlag);
}
