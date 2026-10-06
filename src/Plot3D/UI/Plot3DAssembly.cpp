#include "Plot3DAssembly.h"

#include "AnalysisColorRamp.h"
#include "Material.h"
#include "ModelViewer.h"
#include "Plot3DAxisController.h"
#include "Plot3DVoxelStyle.h"
#include "SceneGraph.h"
#include "SceneMesh.h"
#include "SceneNode.h"
#include "SimulationGlyphs.h"
#include "SimulationVolume.h"
#include "ViewportWidget.h"

#include <QCoreApplication>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QImage>
#include <QImageReader>
#include <QPointF>

#include <algorithm>
#include <cmath>
#include <limits>
#include <variant>

namespace
{
	void setError(QString* error, const QString& message)
	{
		if (error)
			*error = message;
	}

	const std::array<Plot3DAxisConfig, 3> kDefaultAxes = { Plot3DAxisConfig{ QStringLiteral("X") }, Plot3DAxisConfig{ QStringLiteral("Y") },
		Plot3DAxisConfig{ QStringLiteral("Z") } };

	// ---- the plot's colours: every plot starts on the Sequential ramp over its own value range -----------------------------------
	void applySequentialColours(SceneMesh* mesh, const std::vector<float>& values, const std::vector<bool>& valid, float lo, float hi)
	{
		// mapToRGBA() (not mapToNormalizedScalarRGBA()) produces FINAL, already-ramped colours; setAnalysisOverlayBanding(0, ...) is
		// main_scene.frag's "bands < 2" continuous path, which displays v_analysisColor.rgb verbatim. The other encoding (raw
		// normalized t in R) is only for the per-pixel discrete-band path - mixing them renders as a bare red channel.
		mesh->setAnalysisOverlayColors(AnalysisColorRamp::mapToRGBA(values, valid, lo, hi, AnalysisColormap::Sequential));
		mesh->setAnalysisOverlayBanding(0, static_cast<int>(AnalysisColormap::Sequential));
	}

	// ---- An image plane's material: unlit, the picture as its colour (alpha-blended when the picture has an alpha channel) ------
	// The GL context must be current: the picture is uploaded here.
	// A texture of this plot's own. The viewport's shared texture cache hands one GL id to every mesh that uses a file, but a mesh deletes
	// its textures when it is destroyed (a cleared Preview, a deleted plot), which leaves the cache pointing at a dead id: the next plot
	// of the same picture would come out black. Uploaded the way the viewport loads textures (not flipped, mipmapped), clamped at the edges.
	GLuint uploadPrivateTexture(const QString& path)
	{
		QImageReader reader(path);
		reader.setAutoTransform(true);
		QImage image = reader.read();
		QOpenGLContext* context = QOpenGLContext::currentContext();
		if (image.isNull() || !context)
			return 0;
		image = image.convertToFormat(QImage::Format_RGBA8888);
		QOpenGLFunctions* gl = context->functions();
		GLuint id = 0;
		gl->glGenTextures(1, &id);
		gl->glBindTexture(GL_TEXTURE_2D, id);
		gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width(), image.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, image.constBits());
		gl->glGenerateMipmap(GL_TEXTURE_2D);
		gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		gl->glBindTexture(GL_TEXTURE_2D, 0);
		return id;
	}

	bool imageMaterial(ViewportWidget* viewport, const QString& path, Material& out, QString* error)
	{
		Q_UNUSED(viewport); // the context just has to be current (the callers made it so)
		QImageReader reader(path);
		if (!reader.canRead())
		{
			setError(error, QCoreApplication::translate("Plot3DPanel", "The image could not be read."));
			return false;
		}
		const bool hasAlpha = QImage::toPixelFormat(reader.imageFormat()).alphaUsage() == QPixelFormat::UsesAlpha;
		Material material(QVector3D(1.0f, 1.0f, 1.0f), 0.0f, 1.0f, 1.0f);
		material.setUnlit(true);
		Material::Texture texture;
		texture.type = "albedo";
		texture.path = path.toStdString();
		texture.hasAlpha = hasAlpha;
		texture.wrapS = GL_CLAMP_TO_EDGE;
		texture.wrapT = GL_CLAMP_TO_EDGE;
		material.setTexture(Material::TextureType::Albedo, texture);
		material.setAlbedoMap(path);
		if (hasAlpha)
			material.setBlendMode(Material::BlendMode::Alpha);
		const GLuint textureId = uploadPrivateTexture(path);
		if (textureId == 0)
		{
			setError(error, QCoreApplication::translate("Plot3DPanel", "The image could not be read."));
			return false;
		}
		material.setAlbedoTextureId(static_cast<int>(textureId)); // the path stays on the material, so a saved document reloads it
		out = material;
		return true;
	}

	// ---- Quiver: one point mesh of arrow sites plus a glyph set owned by the viewport --------------------------------------------
	std::vector<Vertex> upSiteVertices(const Plot3DMeshData& siteMesh)
	{
		return plot3DPrepareUpload(siteMesh, false).vertices; // the sites' normals are placeholders: every vertex gets an up normal
	}

	GlyphSet quiverGlyphs(const Plot3DQuiverData& quiver, const Plot3DDataset& dataset)
	{
		double minimum[3], maximum[3];
		double diagonal = 1.0;
		if (plot3DDataBounds(dataset, minimum, maximum))
		{
			const double dx = maximum[0] - minimum[0], dy = maximum[1] - minimum[1], dz = maximum[2] - minimum[2];
			const double computed = std::sqrt(dx * dx + dy * dy + dz * dz);
			if (computed > 1.0e-9)
				diagonal = computed;
		}
		const float maxArrowLength = static_cast<float>(diagonal * 0.06);

		GlyphSet glyphs;
		glyphs.anchors.reserve(quiver.arrows.size() * 3);
		glyphs.vectors.reserve(quiver.arrows.size() * 3);
		glyphs.values.reserve(quiver.arrows.size());
		glyphs.colors.reserve(quiver.arrows.size() * 3);
		std::vector<float> magnitudes(quiver.arrows.size());
		float magnitudeMinimum = std::numeric_limits<float>::max();
		float magnitudeMaximum = std::numeric_limits<float>::lowest();
		for (std::size_t i = 0; i < quiver.arrows.size(); ++i)
		{
			const Plot3DPoint& vector = quiver.arrows[i].vector;
			magnitudes[i] = std::sqrt(static_cast<float>(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z));
			magnitudeMinimum = std::min(magnitudeMinimum, magnitudes[i]);
			magnitudeMaximum = std::max(magnitudeMaximum, magnitudes[i]);
		}
		if (magnitudeMaximum <= magnitudeMinimum)
			magnitudeMaximum = magnitudeMinimum + 1.0f;
		for (std::size_t i = 0; i < quiver.arrows.size(); ++i)
		{
			const Plot3DQuiver& arrow = quiver.arrows[i];
			glyphs.anchors.insert(glyphs.anchors.end(), { static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(i) });
			const float rawLength = magnitudes[i];
			const float scale = rawLength > 1.0e-9f ? (maxArrowLength * (rawLength / magnitudeMaximum)) / rawLength : 0.0f;
			glyphs.vectors.insert(glyphs.vectors.end(), {
				static_cast<float>(arrow.vector.x) * scale, static_cast<float>(arrow.vector.y) * scale, static_cast<float>(arrow.vector.z) * scale });
			glyphs.values.push_back(rawLength);
			const QColor color = AnalysisColorRamp::colorForNormalized((rawLength - magnitudeMinimum) / (magnitudeMaximum - magnitudeMinimum), AnalysisColormap::Sequential);
			glyphs.colors.insert(glyphs.colors.end(), { static_cast<float>(color.redF()), static_cast<float>(color.greenF()), static_cast<float>(color.blueF()) });
		}
		glyphs.fieldMin = magnitudeMinimum;
		glyphs.fieldMax = magnitudeMaximum;
		glyphs.referenceLength = maxArrowLength;
		return glyphs;
	}

	// ---- Voxel: a volume grid owned by the viewport, attached to a small proxy point mesh -------------------------------------------
	std::vector<Vertex> voxelProxyVertices(const Plot3DVoxelGrid& grid)
	{
		std::vector<Vertex> vertices(8);
		const float minimum[3] = { grid.origin[0], grid.origin[1], grid.origin[2] };
		const float maximum[3] = { grid.origin[0] + grid.dimX, grid.origin[1] + grid.dimY, grid.origin[2] + grid.dimZ };
		for (int corner = 0; corner < 8; ++corner)
		{
			Vertex& vertex = vertices[static_cast<std::size_t>(corner)];
			vertex.Color = glm::vec4(1.0f);
			vertex.Position = glm::vec3((corner & 1) ? maximum[0] : minimum[0],
				(corner & 2) ? maximum[1] : minimum[1], (corner & 4) ? maximum[2] : minimum[2]);
			vertex.Normal = glm::vec3(0.0f, 0.0f, 1.0f);
			vertex.Tangent = glm::vec3(0.0f);
			vertex.Bitangent = glm::vec3(0.0f);
			for (glm::vec2& uv : vertex.TexCoords)
				uv = glm::vec2(0.0f);
		}
		return vertices;
	}

	VolumeGrid voxelVolume(Plot3DVoxelGrid&& grid)
	{
		VolumeGrid volume;
		volume.values = std::move(grid.values);
		volume.dimX = grid.dimX;
		volume.dimY = grid.dimY;
		volume.dimZ = grid.dimZ;
		for (int axis = 0; axis < 3; ++axis)
		{
			volume.origin[axis] = grid.origin[axis];
			volume.voxelSize[axis] = 1.0f;
		}
		volume.fieldMin = 0.0f;
		volume.fieldMax = 1.0f;
		volume.label = QObject::tr("Occupancy");
		return volume;
	}

	QVector<QPointF> voxelOpacity()
	{
		return plot3DVoxelOpacity();
	}

	// ---- previews ------------------------------------------------------------------------------------------------------------------------
	bool showMeshPreview(ModelViewer* viewer, const Plot3DMeshData& data, unsigned int primitiveMode,
		const std::array<Plot3DAxisConfig, 3>& axes, const double minimum[3], const double maximum[3], const QString& title, float opacity,
		const QString& imagePath = QString())
	{
		ViewportWidget* viewport = viewer ? viewer->getViewportWidget() : nullptr;
		if (!viewport || data.empty())
			return false;

		Plot3DMeshUpload upload = plot3DPrepareUpload(data);
		if (opacity < 1.0f)
		{
			const std::vector<float> colours = AnalysisColorRamp::mapToRGBA(
				upload.values, upload.valid, upload.valueMinimum, upload.valueMaximum, AnalysisColormap::Sequential);
			for (std::size_t i = 0; i < upload.vertices.size(); ++i)
				upload.vertices[i].Color = glm::vec4(colours[i * 4], colours[i * 4 + 1], colours[i * 4 + 2], 1.0f);
		}

		Plot3DAxisController controller;
		Plot3DAxisLayout layout;
		QString error;
		if (!controller.buildLayout(axes, minimum, maximum, layout, &error, title))
			return false;

		viewport->makeCurrent();
		Material previewMaterial;
		if (opacity < 1.0f)
		{
			// The colour map is baked into Vertex::Color above. A neutral, unlit material preserves that data RGB across
			// the whole viewport while its common alpha makes the ribbons transparent in the normal scene blend pass.
			previewMaterial = Material(QVector3D(1.0f, 1.0f, 1.0f), 0.0f, 0.65f, opacity);
			previewMaterial.setBlendMode(Material::BlendMode::Alpha);
			previewMaterial.setUnlit(true);
		}
		const bool isImage = !imagePath.isEmpty() && imageMaterial(viewport, imagePath, previewMaterial, nullptr); // the picture itself, as Build shows it
		SceneMesh* mesh = new SceneMesh(viewport->getShader(), QStringLiteral("Plot3D Preview"), upload.vertices, data.indices, {}, previewMaterial, true, primitiveMode);
		if (isImage)
		{
			mesh->setMaterial(previewMaterial);
			mesh->setTextureMaps(previewMaterial);
		}
		viewport->addToDisplay(mesh);
		if (opacity >= 1.0f && !isImage) // a picture has no colour values to ramp
			applySequentialColours(mesh, upload.values, upload.valid, upload.valueMinimum, upload.valueMaximum);
		const QUuid meshUuid = mesh->uuid();
		viewport->doneCurrent();
		viewer->setPlot3DPreview({ meshUuid }, layout);
		return true;
	}

	bool showQuiverPreview(ModelViewer* viewer, const Plot3DDataset& dataset, const QString& title)
	{
		ViewportWidget* viewport = viewer ? viewer->getViewportWidget() : nullptr;
		if (!viewport || !std::holds_alternative<Plot3DQuiverData>(dataset.content))
			return false;
		const Plot3DQuiverData& quiver = std::get<Plot3DQuiverData>(dataset.content);
		Plot3DMeshData siteMesh;
		QString error;
		if (!buildPlot3DQuiverSiteMesh(quiver, siteMesh, &error))
			return false;
		double minimum[3], maximum[3];
		if (!plot3DDataBounds(dataset, minimum, maximum))
			return false;
		Plot3DAxisController controller;
		Plot3DAxisLayout layout;
		if (!controller.buildLayout(dataset.axes, minimum, maximum, layout, &error, title))
			return false;

		const Plot3DMeshUpload upload = plot3DPrepareUpload(siteMesh);
		viewport->makeCurrent();
		SceneMesh* mesh = new SceneMesh(viewport->getShader(), QStringLiteral("Plot3D Preview"), upSiteVertices(siteMesh), siteMesh.indices, {}, Material(), true, GL_POINTS);
		viewport->addToDisplay(mesh);
		applySequentialColours(mesh, upload.values, upload.valid, upload.valueMinimum, upload.valueMaximum);
		const QUuid meshUuid = mesh->uuid();
		viewport->setSimulationGlyphs(meshUuid, quiverGlyphs(quiver, dataset));
		viewport->doneCurrent();
		viewer->setPlot3DPreview({ meshUuid }, layout);
		return true;
	}

	bool showVoxelPreview(ModelViewer* viewer, const Plot3DDataset& dataset, const QString& title, QString* error)
	{
		ViewportWidget* viewport = viewer ? viewer->getViewportWidget() : nullptr;
		if (!viewport || !std::holds_alternative<Plot3DVoxelData>(dataset.content))
			return false;

		Plot3DVoxelGrid grid;
		if (!buildPlot3DVoxelGrid(std::get<Plot3DVoxelData>(dataset.content), grid, error))
			return false;
		double minimum[3], maximum[3];
		if (!plot3DDataBounds(dataset, minimum, maximum))
		{
			setError(error, QObject::tr("The voxel plot has no valid preview bounds."));
			return false;
		}
		Plot3DAxisController controller;
		Plot3DAxisLayout layout;
		if (!controller.buildLayout(dataset.axes, minimum, maximum, layout, error, title))
			return false;

		std::vector<Vertex> vertices = voxelProxyVertices(grid);
		viewport->makeCurrent();
		SceneMesh* mesh = new SceneMesh(viewport->getShader(), QStringLiteral("Plot3D Preview"), vertices, {}, {}, Material(), true, GL_POINTS);
		viewport->addToDisplay(mesh);
		const QUuid meshUuid = mesh->uuid();
		viewport->setSimulationVolume(meshUuid, voxelVolume(std::move(grid)), static_cast<int>(AnalysisColormap::Sequential), voxelOpacity());
		viewport->doneCurrent();
		viewer->setPlot3DPreview({ meshUuid }, layout);
		return true;
	}

	// ---- committing ------------------------------------------------------------------------------------------------------------------------
	SceneNode* addPlotNode(ModelViewer* viewer, const QString& name, const QUuid& meshUuid)
	{
		SceneNode* node = new SceneNode();
		node->nodeUuid = QUuid::createUuid();
		node->name = name;
		SceneNode* parent = viewer->sceneGraph()->root();
		viewer->sceneGraph()->insertChildNode(parent, node, parent->children.size());
		viewer->sceneGraph()->restoreMeshUuid(node, meshUuid, 0);
		return node;
	}

	void finishCommit(ModelViewer* viewer, ViewportWidget* viewport)
	{
		viewport->doneCurrent();
		viewport->updateView();
		viewer->updateDisplayList();
		// A plot added to a document which already contains CAD geometry would otherwise retain that document's prior camera
		// framing (the negative half of a signed bar plot can sit outside the view and look missing). Fit the updated scene once
		// the new mesh participates in its bounds, as simulation-result insertion does.
		viewport->fitAll();
	}

	void bindCsv(Plot3DSession& session, const Plot3DCsvBinding* csv)
	{
		if (!csv)
			return;
		session.editableCsv = true;
		session.csvSource = csv->text;
		session.csvOptions = csv->options;
		session.columnMapping = csv->mapping;
	}

	void startSession(Plot3DSession& session, const QUuid& meshUuid, const Plot3DCommitOptions& options, Plot3DPrimitive primitive,
		const std::array<Plot3DAxisConfig, 3>& axes)
	{
		session.meshUuid = meshUuid;
		session.name = options.baseName;
		session.title = options.title;
		session.primitive = primitive;
		session.axes = axes;
		session.colormap = static_cast<int>(AnalysisColormap::Sequential);
		session.generated = options.generated;
		bindCsv(session, options.csv);
	}

	void setRange(Plot3DSession& session, float lo, float hi)
	{
		session.dataMinimumValue = lo;
		session.dataMaximumValue = hi;
		session.colourMinimum = lo;
		session.colourMaximum = hi;
	}

	QUuid commitQuiver(ModelViewer* viewer, const Plot3DGenerated& generated, const Plot3DCommitOptions& options, QString* status, QString* error)
	{
		const Plot3DDataset& dataset = generated.dataset;
		const Plot3DQuiverData& quiver = std::get<Plot3DQuiverData>(dataset.content);
		Plot3DMeshData siteMesh;
		if (!buildPlot3DQuiverSiteMesh(quiver, siteMesh, error))
			return QUuid();

		ViewportWidget* viewport = viewer->getViewportWidget();
		viewport->makeCurrent();
		// The arrow sites, drawn as a small GL_POINTS SceneMesh (the same constant-screen-size reasoning as Scatter): GlyphSet's
		// anchors are mesh-vertex indices, so the arrows are anchored to and follow THIS mesh.
		SceneMesh* mesh = new SceneMesh(viewport->getShader(), options.baseName, upSiteVertices(siteMesh), siteMesh.indices, {}, Material(), true, GL_POINTS);
		viewport->addToDisplay(mesh);
		const QUuid meshUuid = mesh->uuid();
		addPlotNode(viewer, options.baseName, meshUuid);

		// One glyph construction path supplies both the rendered arrows and the persistent magnitude colour state, so the legend
		// and later colour edits stay honest even when a CSV's optional value column is unrelated.
		GlyphSet glyphSet = quiverGlyphs(quiver, dataset);
		std::vector<float> siteValues = glyphSet.values;
		std::vector<bool> siteValid(siteValues.size(), true);
		const float lo = glyphSet.fieldMin, hi = glyphSet.fieldMax;
		applySequentialColours(mesh, siteValues, siteValid, lo, hi);
		viewport->setSimulationGlyphs(meshUuid, std::move(glyphSet));
		finishCommit(viewer, viewport);

		double dataLo[3], dataHi[3];
		if (plot3DDataBounds(dataset, dataLo, dataHi))
		{
			Plot3DSession session;
			startSession(session, meshUuid, options, Plot3DPrimitive::Quiver, dataset.axes);
			session.title.clear(); // a quiver's heading is set through the axes title below, as it always was
			std::copy(dataLo, dataLo + 3, session.dataMinimum.begin());
			std::copy(dataHi, dataHi + 3, session.dataMaximum.begin());
			session.values = std::move(siteValues);
			session.valid = std::move(siteValid);
			setRange(session, lo, hi);
			viewer->addPlot3DSession(std::move(session));
			if (!options.title.isEmpty())
				viewer->setPlot3DAxisTitle(meshUuid, options.title);
		}
		if (status)
			*status = QCoreApplication::translate("Plot3DPanel", "Built '%1' (%2 arrows).").arg(options.baseName).arg(quiver.arrows.size());
		return meshUuid;
	}

	QUuid commitVoxel(ModelViewer* viewer, const Plot3DGenerated& generated, const Plot3DCommitOptions& options, QString* status, QString* error)
	{
		const Plot3DDataset& dataset = generated.dataset;
		Plot3DVoxelGrid grid;
		if (!buildPlot3DVoxelGrid(std::get<Plot3DVoxelData>(dataset.content), grid, error))
			return QUuid();
		const int dimX = grid.dimX, dimY = grid.dimY, dimZ = grid.dimZ;

		// SimulationVolumeController associates a grid with an ordinary SceneMesh so it follows scene-tree visibility, transforms,
		// deletion and undo. Its normal rendering pass suppresses meshes with a registered volume, so these eight bounds vertices
		// are only the proxy's transform and fit-all extent, never visible point markers.
		std::vector<Vertex> vertices = voxelProxyVertices(grid);
		ViewportWidget* viewport = viewer->getViewportWidget();
		viewport->makeCurrent();
		SceneMesh* mesh = new SceneMesh(viewport->getShader(), options.baseName, vertices, {}, {}, Material(), true, GL_POINTS);
		viewport->addToDisplay(mesh);
		const QUuid meshUuid = mesh->uuid();
		addPlotNode(viewer, options.baseName, meshUuid);
		viewport->setSimulationVolume(meshUuid, voxelVolume(std::move(grid)), static_cast<int>(AnalysisColormap::Sequential), voxelOpacity());
		finishCommit(viewer, viewport);

		double dataLo[3], dataHi[3];
		if (plot3DDataBounds(dataset, dataLo, dataHi))
		{
			Plot3DSession session;
			startSession(session, meshUuid, options, Plot3DPrimitive::Voxel, dataset.axes);
			session.title.clear();
			std::copy(dataLo, dataLo + 3, session.dataMinimum.begin());
			std::copy(dataHi, dataHi + 3, session.dataMaximum.begin());
			setRange(session, 0.0f, 1.0f);
			viewer->addPlot3DSession(std::move(session));
		}
		if (status)
			*status = QCoreApplication::translate("Plot3DPanel", "Built '%1' (%2 supplied voxels, %3 x %4 x %5 grid).").arg(options.baseName)
				.arg(std::get<Plot3DVoxelData>(dataset.content).voxels.size()).arg(dimX).arg(dimY).arg(dimZ);
		return meshUuid;
	}

	// A plot drawn by one scene mesh (and, for stems and error bars, a companion points mesh): the finished mesh, how it is drawn,
	// and the extent of the data. `dataset` is the plot's dataset when it has one (its flags and sources are kept for editing).
	QUuid commitMeshPlot(ModelViewer* viewer, const Plot3DGenerated& generated, const Plot3DMeshData& meshData, unsigned int primitiveMode,
		const double dataLo[3], const double dataHi[3], const Plot3DCommitOptions& options, QString* status, QString* error)
	{
		const Plot3DDataset* dataset = generated.hasDataset ? &generated.dataset : nullptr;
		const Plot3DPrimitive primitive = generated.primitive;
		const Plot3DMeshOptions& meshOptions = options.mesh;
		const bool isScatter = primitive == Plot3DPrimitive::Scatter;
		const bool drawStems = isScatter && meshOptions.stems;
		const bool drawErrorBars = isScatter && meshOptions.errorBars;
		const bool drawScatterFill = (isScatter || primitive == Plot3DPrimitive::Line) && meshOptions.filled; // transparent ribbons: filled scatter or filled line

		ViewportWidget* viewport = viewer->getViewportWidget();
		viewport->makeCurrent();
		// Opaque plots receive their colour-by-value overlay after construction. Filled scatter ribbons need material alpha
		// blending, so their mapped RGB is baked into Vertex::Color instead; combining the analysis overlay with a transparent
		// material loses the ramp in the main scene shader.
		Plot3DMeshUpload upload = plot3DPrepareUpload(meshData);
		if (drawScatterFill)
		{
			const std::vector<float> colours = AnalysisColorRamp::mapToRGBA(upload.values, upload.valid, upload.valueMinimum, upload.valueMaximum, AnalysisColormap::Sequential);
			for (std::size_t i = 0; i < upload.vertices.size(); ++i)
				upload.vertices[i].Color = glm::vec4(colours[i * 4], colours[i * 4 + 1], colours[i * 4 + 2], 1.0f);
		}
		Material plotMaterial = drawScatterFill ? Material(QVector3D(1.0f, 1.0f, 1.0f), 0.0f, 0.65f, 0.35f) : Material();
		const bool isImage = !generated.imagePath.isEmpty();
		if (isImage && !imageMaterial(viewport, generated.imagePath, plotMaterial, error))
		{
			viewport->doneCurrent();
			return QUuid();
		}
		if (drawScatterFill)
		{
			plotMaterial.setBlendMode(Material::BlendMode::Alpha);
			plotMaterial.setUnlit(true);
		}
		// Line / Scatter draw as native GL_LINE_STRIP / GL_POINTS (meshData.indices is empty for them): the primitive-mode path glTF
		// line / point-cloud import also uses, rendered at a fixed PIXEL size regardless of camera zoom (SceneMesh::draw()).
		// skipOptimization = true: both setAnalysisOverlayColors() and the filled scatter's baked colours are indexed by vertex; the
		// mesh optimiser would reorder vertices (see SceneMesh::optimizeMesh()).
		SceneMesh* mesh = new SceneMesh(viewport->getShader(), options.baseName, upload.vertices, meshData.indices, {}, plotMaterial, true, primitiveMode);
		if (isImage)
		{
			mesh->setMaterial(plotMaterial);
			mesh->setTextureMaps(plotMaterial);
		}
		viewport->addToDisplay(mesh);
		const QUuid meshUuid = mesh->uuid();

		SceneMesh* markerMesh = nullptr;
		QUuid markerMeshUuid;
		std::vector<float> markerValues;
		std::vector<bool> markerValid;
		if ((drawStems || drawErrorBars) && dataset && std::holds_alternative<Plot3DScatterData>(dataset->content))
		{
			// GL_LINES cannot draw endpoint dots. Keep a companion native-points mesh in the same scene node so stems retain their
			// thin, zoom-invariant lines while their sample locations remain immediately readable.
			Plot3DMeshData markerData;
			if (!buildPlot3DScatterMesh(std::get<Plot3DScatterData>(dataset->content), markerData, error))
			{
				viewport->doneCurrent();
				return QUuid();
			}
			Plot3DMeshUpload markerUpload = plot3DPrepareUpload(markerData, false);
			markerValues = std::move(markerUpload.values);
			markerValid = std::move(markerUpload.valid);
			markerMesh = new SceneMesh(viewport->getShader(), options.baseName + QCoreApplication::translate("Plot3DPanel", " Markers"), markerUpload.vertices, {}, {}, Material(), true, GL_POINTS);
			viewport->addToDisplay(markerMesh);
			markerMeshUuid = markerMesh->uuid();
		}

		SceneNode* node = addPlotNode(viewer, options.baseName, meshUuid);
		if (markerMesh)
			viewer->sceneGraph()->restoreMeshUuid(node, markerMeshUuid, 1);

		// Colour by value, unless every sample's value is NaN (a plain uniform-Z surface with no separate colour data).
		if (upload.anyValid && !drawScatterFill)
		{
			applySequentialColours(mesh, upload.values, upload.valid, upload.valueMinimum, upload.valueMaximum);
			if (markerMesh)
				applySequentialColours(markerMesh, markerValues, markerValid, upload.valueMinimum, upload.valueMaximum);
		}
		finishCommit(viewer, viewport);

		Plot3DSession session;
		startSession(session, meshUuid, options, primitive, dataset ? dataset->axes : kDefaultAxes);
		session.markerMeshUuid = markerMeshUuid;
		std::copy(dataLo, dataLo + 3, session.dataMinimum.begin());
		std::copy(dataHi, dataHi + 3, session.dataMaximum.begin());
		session.values = std::move(upload.values);
		session.valid = std::move(upload.valid);
		session.markerValues = std::move(markerValues);
		session.markerValid = std::move(markerValid);
		setRange(session, upload.anyValid ? upload.valueMinimum : 0.0f, upload.anyValid ? upload.valueMaximum : 1.0f);
		session.isStem = drawStems;
		session.isErrorBars = drawErrorBars;
		session.isFilledScatter = drawScatterFill;
		session.scatterBaseZ = meshOptions.baseZ;
		if (dataset && primitive == Plot3DPrimitive::Bar)
			session.barSource = std::get<Plot3DBarData>(dataset->content);
		if (dataset && primitive == Plot3DPrimitive::Contour)
			session.contourSource = std::get<Plot3DSurfaceData>(dataset->content);
		viewer->addPlot3DSession(std::move(session));
		if (options.contourOverlayMode != 0 && primitive == Plot3DPrimitive::Surface)
			viewer->setPlot3DContourOverlay(meshUuid, options.contourOverlayMode, 10);

		if (status)
		{
			if (primitive == Plot3DPrimitive::Bar && dataset)
				*status = QCoreApplication::translate("Plot3DPanel", "Built '%1' (%2 bars).").arg(options.baseName).arg(std::get<Plot3DBarData>(dataset->content).bars.size());
			else if (drawStems && dataset)
				*status = QCoreApplication::translate("Plot3DPanel", "Built '%1' (%2 stems).").arg(options.baseName).arg(std::get<Plot3DScatterData>(dataset->content).samples.size());
			else if (drawScatterFill && dataset && std::holds_alternative<Plot3DLineData>(dataset->content))
				*status = QCoreApplication::translate("Plot3DPanel", "Built '%1' (%2 filled segments).").arg(options.baseName).arg(std::get<Plot3DLineData>(dataset->content).samples.size() - 1);
			else if (drawScatterFill && dataset)
				*status = QCoreApplication::translate("Plot3DPanel", "Built '%1' (%2 filled ribbons).").arg(options.baseName).arg(std::get<Plot3DScatterData>(dataset->content).samples.size());
			else if (!dataset && primitiveMode == Plot3DGl::kTriangles)
				*status = QCoreApplication::translate("Plot3DPanel", "Built '%1' (%2 vertices).").arg(options.baseName).arg(meshData.vertexCount());
			else
				*status = QCoreApplication::translate("Plot3DPanel", "Built '%1' (%2 points).").arg(options.baseName).arg(meshData.vertexCount());
		}
		return meshUuid;
	}
}

void plot3DRescaleVertices(std::vector<Vertex>& vertices, const std::array<Plot3DAxisConfig, 3>& from, const std::array<Plot3DAxisConfig, 3>& to)
{
	if (plot3DSameAxisScale(from[0], to[0]) && plot3DSameAxisScale(from[1], to[1]) && plot3DSameAxisScale(from[2], to[2]))
		return;
	for (Vertex& vertex : vertices)
	{
		double stretch[3] = { 1.0, 1.0, 1.0 }; // to's slope over from's slope, per axis
		for (int axis = 0; axis < 3; ++axis)
		{
			const double data = plot3DInverseAxisValue(vertex.Position[axis], from[axis]);
			bool ok = false;
			const double placed = plot3DTransformAxisValue(data, to[axis], &ok);
			if (!ok || !std::isfinite(placed))
				continue;
			vertex.Position[axis] = static_cast<float>(placed);
			stretch[axis] = plot3DAxisScaleSlope(data, to[axis]) / plot3DAxisScaleSlope(data, from[axis]);
		}
		// A normal is a covector: it scales by the inverse of the stretch.
		const glm::vec3 normal(vertex.Normal.x / static_cast<float>(stretch[0]), vertex.Normal.y / static_cast<float>(stretch[1]),
			vertex.Normal.z / static_cast<float>(stretch[2]));
		const float length = glm::length(normal);
		if (length > 1.0e-12f && std::isfinite(length))
			vertex.Normal = normal / length;
	}
}

Plot3DMeshUpload plot3DPrepareUpload(const Plot3DMeshData& data, bool dataNormals)
{
	Plot3DMeshUpload upload;
	const std::size_t count = data.vertexCount();
	upload.vertices.resize(count);
	upload.values.resize(count);
	upload.valid.resize(count);
	const bool haveNormals = dataNormals && data.normals.size() == data.positions.size();
	float lo = std::numeric_limits<float>::max(), hi = std::numeric_limits<float>::lowest();
	for (int axis = 0; axis < 3; ++axis)
	{
		upload.boundsMinimum[static_cast<std::size_t>(axis)] = std::numeric_limits<double>::max();
		upload.boundsMaximum[static_cast<std::size_t>(axis)] = std::numeric_limits<double>::lowest();
	}
	for (std::size_t i = 0; i < count; ++i)
	{
		Vertex& vertex = upload.vertices[i];
		vertex.Color = glm::vec4(1.0f);
		vertex.Position = glm::vec3(data.positions[i * 3], data.positions[i * 3 + 1], data.positions[i * 3 + 2]);
		vertex.Normal = haveNormals ? glm::vec3(data.normals[i * 3], data.normals[i * 3 + 1], data.normals[i * 3 + 2]) : glm::vec3(0.0f, 0.0f, 1.0f);
		vertex.Tangent = glm::vec3(0.0f);
		vertex.Bitangent = glm::vec3(0.0f);
		for (glm::vec2& uv : vertex.TexCoords)
			uv = glm::vec2(0.0f);
		if (data.uvs.size() == count * 2)
			vertex.TexCoords[0] = glm::vec2(data.uvs[i * 2], data.uvs[i * 2 + 1]);
		const bool finite = std::isfinite(data.values[i]);
		upload.valid[i] = finite;
		upload.values[i] = finite ? static_cast<float>(data.values[i]) : 0.0f;
		if (finite)
		{
			lo = std::min(lo, upload.values[i]);
			hi = std::max(hi, upload.values[i]);
		}
		for (int axis = 0; axis < 3; ++axis)
		{
			const double position = static_cast<double>(data.positions[i * 3 + static_cast<std::size_t>(axis)]);
			upload.boundsMinimum[static_cast<std::size_t>(axis)] = std::min(upload.boundsMinimum[static_cast<std::size_t>(axis)], position);
			upload.boundsMaximum[static_cast<std::size_t>(axis)] = std::max(upload.boundsMaximum[static_cast<std::size_t>(axis)], position);
		}
	}
	upload.anyValid = hi >= lo;
	if (upload.anyValid)
	{
		upload.valueMinimum = lo;
		upload.valueMaximum = hi > lo ? hi : lo + 1.0f; // a perfectly flat field still needs a non-degenerate range for the colour ramp
	}
	return upload;
}

bool plot3DShowPreview(ModelViewer* viewer, const Plot3DGenerated& generated, const Plot3DMeshOptions& options, const QString& title, QString* error)
{
	if (!viewer || !viewer->getViewportWidget())
		return false;
	if (generated.hasDataset)
	{
		if (generated.primitive == Plot3DPrimitive::Quiver)
		{
			if (!showQuiverPreview(viewer, generated.dataset, title))
			{
				setError(error, QCoreApplication::translate("Plot3DPanel", "The quiver preview could not be created."));
				return false;
			}
			return true;
		}
		if (generated.primitive == Plot3DPrimitive::Voxel)
		{
			if (!showVoxelPreview(viewer, generated.dataset, title, error))
			{
				if (error && error->isEmpty())
					*error = QCoreApplication::translate("Plot3DPanel", "The voxel preview could not be created.");
				return false;
			}
			return true;
		}
		Plot3DMeshData mesh;
		unsigned int mode = Plot3DGl::kTriangles;
		if (!plot3DMeshForDataset(generated.dataset, options, mesh, mode, error))
			return false;
		double minimum[3], maximum[3];
		if (!plot3DDatasetBounds(generated.dataset, options, minimum, maximum))
		{
			setError(error, QCoreApplication::translate("Plot3DPanel", "The plot has no valid preview bounds."));
			return false;
		}
		const bool filledScatter = (generated.primitive == Plot3DPrimitive::Scatter || generated.primitive == Plot3DPrimitive::Line) && options.filled;
		if (!showMeshPreview(viewer, mesh, mode, generated.dataset.axes, minimum, maximum, title, filledScatter ? 0.35f : 1.0f))
		{
			setError(error, QCoreApplication::translate("Plot3DPanel", "The plot preview could not be created."));
			return false;
		}
		return true;
	}
	double minimum[3], maximum[3];
	if (!plot3DMeshBounds(generated.mesh, minimum, maximum))
	{
		setError(error, QCoreApplication::translate("Plot3DPanel", "The plot has no valid preview bounds."));
		return false;
	}
	if (!showMeshPreview(viewer, generated.mesh, generated.primitiveMode, kDefaultAxes, minimum, maximum, title, 1.0f, generated.imagePath))
	{
		setError(error, QCoreApplication::translate("Plot3DPanel", "The plot preview could not be created."));
		return false;
	}
	return true;
}

QUuid plot3DCommit(ModelViewer* viewer, const Plot3DGenerated& generated, const Plot3DCommitOptions& options, QString* status, QString* error)
{
	if (!viewer || !viewer->getViewportWidget() || !viewer->sceneGraph())
		return QUuid();
	if (generated.hasDataset && generated.primitive == Plot3DPrimitive::Quiver)
		return commitQuiver(viewer, generated, options, status, error);
	if (generated.hasDataset && generated.primitive == Plot3DPrimitive::Voxel)
		return commitVoxel(viewer, generated, options, status, error);

	Plot3DMeshData mesh;
	unsigned int mode = generated.primitiveMode;
	double dataLo[3], dataHi[3];
	if (generated.hasDataset)
	{
		if (!plot3DMeshForDataset(generated.dataset, options.mesh, mesh, mode, error))
			return QUuid();
		if (!plot3DDatasetBounds(generated.dataset, options.mesh, dataLo, dataHi))
		{
			setError(error, QCoreApplication::translate("Plot3DPanel", "The plot has no valid preview bounds."));
			return QUuid();
		}
	}
	else
	{
		mesh = generated.mesh;
		if (!plot3DMeshBounds(mesh, dataLo, dataHi))
		{
			setError(error, QCoreApplication::translate("Plot3DPanel", "The plot has no valid preview bounds."));
			return QUuid();
		}
	}
	return commitMeshPlot(viewer, generated, mesh, mode, dataLo, dataHi, options, status, error);
}

bool plot3DRebuild(ModelViewer* viewer, const QUuid& meshUuid, const Plot3DGenerated& generated, const Plot3DCsvBinding* csv, QString* error,
	const double* baseZ)
{
	if (!viewer || !viewer->getViewportWidget())
		return false;
	const QVector<Plot3DSession> sessions = viewer->plot3DSessions();
	const auto found = std::find_if(sessions.cbegin(), sessions.cend(), [&meshUuid](const Plot3DSession& session) { return session.meshUuid == meshUuid; });
	if (found == sessions.cend() || found->primitive != generated.primitive)
	{
		setError(error, QCoreApplication::translate("Plot3DPanel", "The plot is no longer available or its primitive changed."));
		return false;
	}

	// A mesh source (parametric / implicit surface, streamlines, pathlines, time series): swap in the new mesh.
	if (!generated.hasDataset)
	{
		if (!viewer->replacePlot3DMesh(meshUuid, generated.mesh, generated.primitiveMode))
		{
			setError(error, QCoreApplication::translate("Plot3DPanel", "The plot could not be rebuilt."));
			return false;
		}
		if (!generated.imagePath.isEmpty())
		{
			ViewportWidget* viewport = viewer->getViewportWidget();
			SceneMesh* imageMesh = viewport->getMeshByUuid(meshUuid);
			Material material;
			viewport->makeCurrent();
			const bool ok = imageMesh && imageMaterial(viewport, generated.imagePath, material, error);
			if (ok)
			{
				const GLuint replaced = static_cast<GLuint>(imageMesh->getMaterial().albedoTextureId());
				imageMesh->setMaterial(material);
				imageMesh->setTextureMaps(material);
				if (replaced != 0 && QOpenGLContext::currentContext())
					QOpenGLContext::currentContext()->functions()->glDeleteTextures(1, &replaced); // the previous picture's own texture
			}
			viewport->doneCurrent();
			if (!ok)
				return false;
			viewport->updateView();
		}
		if (csv && found->generated.valid && found->generated.sourceMode == plot3DSourceInt(Plot3DSourceKind::CsvTimeSeries))
			viewer->setPlot3DTimeSeriesSource(meshUuid, csv->text, csv->options, csv->mapping); // the table travels with the plot
		return true;
	}

	// A dataset source (CSV tables, formula surfaces and contours, curves, vector fields).
	const Plot3DDataset& dataset = generated.dataset;
	Plot3DSession updated = *found;
	ViewportWidget* viewport = viewer->getViewportWidget();
	SceneMesh* mesh = viewport->getMeshByUuid(updated.meshUuid);
	if (!mesh)
		return false;

	// The presentation controls own these settings; a rebuild keeps them.
	Plot3DMeshOptions options;
	options.contourLevels = updated.contourLevels;
	options.contourProjected = updated.contourProjected;
	options.stems = updated.isStem;
	options.errorBars = updated.isErrorBars;
	options.filled = updated.isFilledScatter;
	if (baseZ)
		updated.scatterBaseZ = *baseZ; // the dialog's Base Z (stems, filled scatter / line) is editable; the rest stays as presented
	options.baseZ = updated.scatterBaseZ;
	options.barWidthScale = updated.barWidthScale;
	options.barDepthScale = updated.barDepthScale;

	auto copyValues = [](const Plot3DMeshData& data, std::vector<float>& values, std::vector<bool>& valid) {
		const Plot3DMeshUpload upload = plot3DPrepareUpload(data, false);
		values = upload.values;
		valid = upload.valid;
	};

	float newValueMinimum = std::numeric_limits<float>::max();
	float newValueMaximum = std::numeric_limits<float>::lowest();
	bool haveValues = false;
	viewport->makeCurrent();
	if (dataset.primitive == Plot3DPrimitive::Quiver)
	{
		const Plot3DQuiverData& quiver = std::get<Plot3DQuiverData>(dataset.content);
		Plot3DMeshData sites;
		if (!buildPlot3DQuiverSiteMesh(quiver, sites, error))
		{
			viewport->doneCurrent();
			return false;
		}
		mesh->setPrimitiveMode(GL_POINTS);
		mesh->setMeshData(upSiteVertices(sites), sites.indices);
		GlyphSet glyphs = quiverGlyphs(quiver, dataset);
		updated.values = glyphs.values;
		updated.valid.assign(updated.values.size(), true);
		newValueMinimum = glyphs.fieldMin; newValueMaximum = glyphs.fieldMax; haveValues = true;
		viewport->setSimulationGlyphs(updated.meshUuid, std::move(glyphs));
		// Replacing a GlyphSet resets its controller-owned display scale. Restore the session setting just as the mesh-owned line
		// and marker sizes remain intact when setMeshData() replaces ordinary plot geometry.
		viewport->setSimulationGlyphScale(updated.meshUuid, updated.arrowScale);
	}
	else if (dataset.primitive == Plot3DPrimitive::Voxel)
	{
		Plot3DVoxelGrid grid;
		if (!buildPlot3DVoxelGrid(std::get<Plot3DVoxelData>(dataset.content), grid, error))
		{
			viewport->doneCurrent();
			return false;
		}
		mesh->setPrimitiveMode(GL_POINTS);
		mesh->setMeshData(voxelProxyVertices(grid), {});
		viewport->setSimulationVolume(updated.meshUuid, voxelVolume(std::move(grid)), updated.colormap, voxelOpacity());
		updated.values.clear(); updated.valid.clear();
		newValueMinimum = 0.0f; newValueMaximum = 1.0f; haveValues = true;
	}
	else
	{
		Plot3DMeshData data;
		unsigned int mode = Plot3DGl::kTriangles;
		if (!plot3DMeshForDataset(dataset, options, data, mode, error))
		{
			viewport->doneCurrent();
			return false;
		}
		mesh->setPrimitiveMode(mode);
		Plot3DMeshUpload rebuilt = plot3DPrepareUpload(data);
		plot3DRescaleVertices(rebuilt.vertices, std::array<Plot3DAxisConfig, 3>{}, updated.axes); // the plot may already be on a log / symlog axis
		mesh->setMeshData(rebuilt.vertices, data.indices);
		copyValues(data, updated.values, updated.valid);
		for (std::size_t i = 0; i < updated.values.size(); ++i)
			if (updated.valid[i])
			{
				newValueMinimum = std::min(newValueMinimum, updated.values[i]);
				newValueMaximum = std::max(newValueMaximum, updated.values[i]);
				haveValues = true;
			}
		if (!updated.markerMeshUuid.isNull() && std::holds_alternative<Plot3DScatterData>(dataset.content))
		{
			SceneMesh* marker = viewport->getMeshByUuid(updated.markerMeshUuid);
			Plot3DMeshData markerData;
			if (marker && buildPlot3DScatterMesh(std::get<Plot3DScatterData>(dataset.content), markerData, error))
			{
				marker->setPrimitiveMode(GL_POINTS);
				Plot3DMeshUpload markerRebuilt = plot3DPrepareUpload(markerData);
				plot3DRescaleVertices(markerRebuilt.vertices, std::array<Plot3DAxisConfig, 3>{}, updated.axes);
				marker->setMeshData(markerRebuilt.vertices, {});
				copyValues(markerData, updated.markerValues, updated.markerValid);
			}
		}
		if (dataset.primitive == Plot3DPrimitive::Contour)
			updated.contourSource = std::get<Plot3DSurfaceData>(dataset.content);
		if (dataset.primitive == Plot3DPrimitive::Bar)
			updated.barSource = std::get<Plot3DBarData>(dataset.content);
	}
	viewport->doneCurrent();

	double minimum[3]{}, maximum[3]{};
	if (!plot3DDatasetBounds(dataset, options, minimum, maximum))
		return false;
	std::copy(minimum, minimum + 3, updated.dataMinimum.begin());
	std::copy(maximum, maximum + 3, updated.dataMaximum.begin());
	if (haveValues)
	{
		if (newValueMaximum <= newValueMinimum)
			newValueMaximum = newValueMinimum + 1.0f;
		updated.dataMinimumValue = newValueMinimum;
		updated.dataMaximumValue = newValueMaximum;
		if (updated.automaticColourRange)
		{
			updated.colourMinimum = newValueMinimum;
			updated.colourMaximum = newValueMaximum;
		}
	}
	if (updated.editableCsv && csv) // a generated plot has no table to keep
	{
		updated.csvSource = csv->text;
		updated.csvOptions = csv->options;
		updated.columnMapping = csv->mapping;
	}
	viewer->updatePlot3DSession(std::move(updated));
	viewer->refreshPlot3DContourOverlay(meshUuid); // the surface changed, so its iso-lines must follow
	viewport->updateView();
	viewer->updateDisplayList();
	return true;
}
