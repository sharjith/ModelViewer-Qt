#pragma once

#include <QString>
#include <QtGlobal>
#include <chrono>

// Wall-clock totals of the stages of creating the viewer meshes of one import, logged once as "[MeshBuildProfile]".
// Active only between begin() and end(), which ViewportWidget::consumeMeshBatch() calls; free otherwise.
namespace MeshBuildProfile
{
enum Stage
{
	CreateTotal,       // createMeshFromData() as a whole
	BuilderPre,        // AssImpMeshBuilder::build() before the SceneMesh is constructed (textures, material)
	SceneMeshTotal,    // the SceneMesh constructor as a whole
	CtorBody,          // constructor up to setupMesh()
	SetupArrays,       // setupMesh(): vertex struct -> separate arrays
	GlUpload,          // initBuffers(): GL buffer creation and upload
	LodAndBounds,      // LOD tier upload and bounds
	FeatureEdges,      // feature-edge extraction and upload
	BaseGlObjects,     // RenderableMesh constructor: glGenBuffers/glGenVertexArrays for the mesh's buffer objects
	BaseAutoName,      // RenderableMesh constructor: setAutoIncrName()
	BuilderPost,       // AssImpMeshBuilder::build() after the constructor
	Attach,            // addToDisplay() and the per-mesh bookkeeping after it
	UpdateDisplayList, // the single display-list update at the end of the batch
	GlAlloc,           // inside GlUpload: the glBufferData calls (index, position, normal, colour, tangent, ...)
	GlAttrib,          // inside GlUpload: VAO attribute setup
	RuntimeBounds,     // inside GlUpload: updateRuntimeBounds() (CPU, picking triangles and bounds)
	StageCount
};

inline bool& active() { static bool a = false; return a; }
inline qint64* totals() { static qint64 t[StageCount] = {}; return t; }
inline qint64& meshCount() { static qint64 c = 0; return c; }
inline qint64 now()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline void begin()
{
	for (int i = 0; i < StageCount; ++i)
		totals()[i] = 0;
	meshCount() = 0;
	active() = true;
}

inline QString end()
{
	active() = false;
	static const char* names[StageCount] = { "createTotal", "builderPre", "sceneMeshTotal", "ctorBody", "setupArrays", "glUpload",
		"lodAndBounds", "featureEdges", "baseGlObjects", "baseAutoName", "builderPost", "attach", "updateDisplayList", "inGlAlloc", "inGlAttrib", "inRuntimeBounds" };
	QString text = QStringLiteral("[MeshBuildProfile] meshes=%1").arg(meshCount());
	for (int i = 0; i < StageCount; ++i)
		text += QStringLiteral(" %1=%2ms").arg(QLatin1String(names[i])).arg(totals()[i] / 1000000);
	return text;
}

// Adds the time from construction (or the last stop()) to destruction to a stage, when active.
class Lap
{
public:
	explicit Lap(Stage stage) : _stage(stage), _start(active() ? now() : 0) {}
	~Lap() { stop(); }
	void stop()
	{
		if (_start != 0)
		{
			totals()[_stage] += now() - _start;
			_start = 0;
		}
	}

private:
	Stage _stage;
	qint64 _start;
};
}
