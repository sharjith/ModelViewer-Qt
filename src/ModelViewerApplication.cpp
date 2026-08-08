#include "ModelViewerApplication.h"
#include <QOpenGLFunctions>
#include <QSettings>
#include <QSurfaceFormat>
#include <QTranslator>
#include <QLocale>
#include <assimp/Importer.hpp>
#include <config.h>

namespace AppContext
{
	QString& SessionId()
	{
		static QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
		return id;
	}
}

QStringList ModelViewerApplication::_supportedExtensions;
int ModelViewerApplication::_supportedMSAASamples = 4; // Default MSAA samples
int ModelViewerApplication::_supportedAnisotropicFilteringLevel = 16; // Default anisotropic filtering level

void ModelViewerApplication::configureOpenGLAttributes()
{
	// These Qt application attributes must be set before QApplication is constructed.
	// AA_ShareOpenGLContexts is needed on every platform now that Qt-ADS document
	// tab switches can recreate QOpenGLWidget contexts in ordinary use. With
	// sharing enabled, textures/VBOs survive those recreations and only truly
	// context-local objects (VAOs/FBOs/etc.) need rebuilding, which keeps tab
	// switching responsive. AA_UseDesktopOpenGL remains Linux-only.
	QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
#if !defined(Q_OS_WIN)
	QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
#endif
}

ModelViewerApplication::ModelViewerApplication(int& argc, char** argv)
    : QApplication(argc, argv)
{
	setDesktopSettingsAware(true);
	setApplicationName("ModelViewer");
	setOrganizationName("Sharjith N");

	QString version = QString(APP_VERSION_STRING);
	setApplicationVersion(version);


	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	int values[] = {0, 2, 4, 8, 16, 32};
	int samples = values[settings.value("msaaComboBox", 4).toInt()];
	bool vsyncEnabled = settings.value("vsyncCheckBox", true).toBool();

	QSurfaceFormat format;
	format.setDepthBufferSize(24);
	format.setStencilBufferSize(8);
	// No per-pixel window alpha: on native Wayland the compositor alpha-blends
	// the surface against the desktop using whatever the framebuffer's alpha
	// channel holds, and PBR/RT blending (floor transparency, glass/transmission,
	// RtPresenter's GL_SRC_ALPHA compositing) leaves destination alpha < 1 in
	// places - without this, those pixels show the desktop behind the window.
	format.setAlphaBufferSize(0);
	format.setVersion(4, 5); // OpenGL version 4.5
	format.setProfile(QSurfaceFormat::CoreProfile);
	format.setOption(QSurfaceFormat::DebugContext);
	format.setSwapInterval(vsyncEnabled ? 1 : 0);
	format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
	format.setRenderableType(QSurfaceFormat::OpenGL);
	format.setSamples(samples); // Set MSAA samples

	QSurfaceFormat::setDefaultFormat(format);

}

QStringList ModelViewerApplication::supportedImportExtensions()
{
    if(_supportedExtensions.isEmpty()) {
		initializeSupportedImportExtensions();
	}
	return _supportedExtensions;
}

void ModelViewerApplication::initializeSupportedImportExtensions()
{
	// 1. Get supported extensions from Assimp
	Assimp::Importer importer;
	std::string extList;
	importer.GetExtensionList(extList); // E.g. "*.obj;*.3ds;*.fbx;..."
	QString allExtensions = QString::fromStdString(extList).replace(';', ' ');

	// 2. Manually add STEP/IGES extensions (if not in Assimp list)
	if (!allExtensions.contains("*.step", Qt::CaseInsensitive)) {
		allExtensions += " *.step *.stp";
	}
	if (!allExtensions.contains("*.iges", Qt::CaseInsensitive)) {
		allExtensions += " *.iges *.igs";
	}
	if (!allExtensions.contains("*.brep", Qt::CaseInsensitive))
	{
		allExtensions += " *.brep *.rle";
	}

	// 3. All Supported filter
	QString allSupportedFilter = QString("All Supported Files (%1)").arg(allExtensions.trimmed());

	// 4. Common filters list
	QStringList commonFilters = {
		"Wavefront OBJ (*.obj)",
		"Autodesk 3DS (*.3ds)",
		"Collada DAE (*.dae)",
		"STL (*.stl)",
		"FBX (*.fbx)",
		"PLY (*.ply)",
		"DXF (*.dxf)",
		"GLTF (*.gltf *.glb)",
		"STEP (*.step *.stp)",
		"IGES (*.iges *.igs)",
		"BREP (*.brep *.rle)",
		"IFC (*.ifc)",
		"OFF (*.off)",
		"LWO (*.lwo *.lws)",
		"AC3D (*.ac *.ac3d *.acc)",
		"Blender (*.blend)",
		"Irrlicht (*.irr *.irrmesh)",
		"MD5 (*.md5mesh *.md5anim *.md5camera)"
	};

	// 5. Add all filters to dialog	
	_supportedExtensions << allSupportedFilter << commonFilters;
}
