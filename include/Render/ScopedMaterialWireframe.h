// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QOpenGLFunctions_4_5_Core>

// A material-local raster style, independent of the caller's viewport mode.
// Desktop core OpenGL changes polygon mode for both faces together. Restore the
// actual incoming mode, including GL_LINE / GL_POINT, rather than assuming fill.
class ScopedMaterialWireframe
{
public:
    ScopedMaterialWireframe(QOpenGLFunctions_4_5_Core& gl, bool enabled)
    {
        if (!enabled)
            return;
        // Reserve two slots for drivers exposing the compatibility query shape;
        // core contexts may return only the single shared polygon mode.
        GLint modes[2] = {GL_FILL, GL_FILL};
        gl.glGetIntegerv(GL_POLYGON_MODE, modes);
        if (modes[0] == GL_LINE)
            return;
        _gl = &gl;
        _previousMode = static_cast<GLenum>(modes[0]);
        gl.glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }

    ~ScopedMaterialWireframe()
    {
        if (_gl)
            _gl->glPolygonMode(GL_FRONT_AND_BACK, _previousMode);
    }

    ScopedMaterialWireframe(const ScopedMaterialWireframe&) = delete;
    ScopedMaterialWireframe& operator=(const ScopedMaterialWireframe&) = delete;

private:
    QOpenGLFunctions_4_5_Core* _gl = nullptr;
    GLenum _previousMode = GL_FILL;
};
