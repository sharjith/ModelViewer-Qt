#pragma once

#include "IGpuContextResource.h"
#include <QObject>
#include <QColor>
#include <QPoint>
#include <QRect>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_4_5_Core>
#include <QOpenGLVertexArrayObject>
#include <QVector3D>

class ConeRenderable;
class Camera;
class ShaderProgram;

class TransformGizmo : public QObject, protected QOpenGLFunctions_4_5_Core, public IGpuContextResource
{
	Q_OBJECT
public:
	enum class Handle
	{
		None,
		TranslateX,
		TranslateY,
		TranslateZ,
		UniformScale,
		RotateXY,
		RotateYZ,
		RotateZX
	};

	explicit TransformGizmo(QObject* parent = nullptr);
	~TransformGizmo() override;

	// IGpuContextResource - registered once from ViewportWidget's
	// constructor (this object is constructed once for the widget's whole
	// lifetime, never deleted/recreated on ordinary context recreation).
	// restoreGpuResources() was formerly private ensureInitialized(),
	// called lazily from render() every frame (cheap no-op once already
	// initialized) - now also called explicitly by the registry on every
	// context recreation, which is what actually fixes the bug: previously
	// nothing ever re-created this object after the first context loss, so
	// the gizmo silently vanished for the rest of the session.
	void restoreGpuResources() override;
	void releaseGpuResources() override;

	void setVisible(bool visible);
	bool isVisible() const { return _visible; }

	void setPivot(const QVector3D& pivot) { _pivot = pivot; }
	QVector3D pivot() const { return _pivot; }
	Handle hoveredHandle() const { return _hoveredHandle; }
	Handle activeHandle() const { return _activeHandle; }
	void clearInteraction();
	bool updateHover(const QPoint& pixel, const Camera* camera,
	                 const QMatrix4x4& viewMatrix, const QMatrix4x4& projectionMatrix,
	                 const QRect& viewport, float fallbackScale);
	bool activateHandleAt(const QPoint& pixel, const Camera* camera,
	                      const QMatrix4x4& viewMatrix, const QMatrix4x4& projectionMatrix,
	                      const QRect& viewport, float fallbackScale);

	void render(ShaderProgram* axisShader, ConeRenderable* axisCone, const Camera* camera,
	            const QMatrix4x4& viewMatrix, const QMatrix4x4& projectionMatrix,
	            float fallbackScale);

signals:
	void hoveredHandleChanged(TransformGizmo::Handle handle);
	void activeHandleChanged(TransformGizmo::Handle handle);

private:
	float computeWorldScale(const Camera* camera, float fallbackScale) const;
	Handle hitTest(const QPoint& pixel, const Camera* camera,
	               const QMatrix4x4& viewMatrix, const QMatrix4x4& projectionMatrix,
	               const QRect& viewport, float fallbackScale) const;
	QColor resolveHandleColor(Handle handle, const QColor& baseColor) const;
	void drawAxes(ShaderProgram* axisShader, ConeRenderable* axisCone,
	              const QMatrix4x4& viewMatrix, const QMatrix4x4& projectionMatrix,
	              float worldScale);
	void drawScaleHandle(ShaderProgram* axisShader, const QMatrix4x4& viewMatrix,
	                     const QMatrix4x4& projectionMatrix, float worldScale);
	void drawArcs(ShaderProgram* axisShader, const QMatrix4x4& viewMatrix,
	              const QMatrix4x4& projectionMatrix, float worldScale);

	bool _visible = false;
	bool _initialized = false;
	QVector3D _pivot = QVector3D(0.0f, 0.0f, 0.0f);
	Handle _hoveredHandle = Handle::None;
	Handle _activeHandle = Handle::None;

	QOpenGLVertexArrayObject _axesVao;
	QOpenGLBuffer _axesVertexBuffer;
	QOpenGLBuffer _axesColorBuffer;

	QOpenGLVertexArrayObject _arcsVao;
	QOpenGLBuffer _arcsVertexBuffer;
	QOpenGLBuffer _arcsColorBuffer;

	QOpenGLVertexArrayObject _scaleVao;
	QOpenGLBuffer _scaleVertexBuffer;
	QOpenGLBuffer _scaleColorBuffer;

	int _arcVertexCount = 0;
	int _xyArcOffset = 0;
	int _yzArcOffset = 0;
	int _zxArcOffset = 0;
	int _xyArcCount = 0;
	int _yzArcCount = 0;
	int _zxArcCount = 0;
	int _scaleVertexCount = 0;
};
