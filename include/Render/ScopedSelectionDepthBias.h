// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QOpenGLFunctions_4_5_Core>

#include <algorithm>

// Pulls the filled depth of a selected mesh a little toward the camera so its selection colour is not hidden by meshes
// that occupy the same place (assemblies that repeat a body several times at one position are common in CAD exports).
// Without it, which of the coincident meshes wins the depth test depends on draw order and rounding, and a selected mesh
// drawn after its unselected twins (as the batched second pass does) loses every tie.
//
// The bias is relative to the caller's own polygon offset: Mesh Edges and Shaded-with-Edges push the fill back by 1.25 so the
// line overlay can sit on top, and that offset is kept and only reduced. The incoming offset state is restored exactly.
class ScopedSelectionDepthBias
{
public:
    ScopedSelectionDepthBias(QOpenGLFunctions_4_5_Core& gl, bool enabled)
    {
        if (!enabled)
            return;
        _gl = &gl;
        _wasEnabled = gl.glIsEnabled(GL_POLYGON_OFFSET_FILL) == GL_TRUE;
        if (_wasEnabled)
        {
            // The factor and units stay set after glDisable, so they are only meaningful while the offset is enabled.
            gl.glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &_factor);
            gl.glGetFloatv(GL_POLYGON_OFFSET_UNITS, &_units);
        }
        // A caller that pushed the fill back (positive offset) did so to keep a line overlay in front of it: the selected fill may
        // move forward of its coincident twins but must stay behind the overlay, so the reduction stops at a small positive value.
        const float factor = _factor > 0.0f ? (std::max)(_factor - kFactor, kKeepBehindOverlay) : _factor - kFactor;
        const float units = _units > 0.0f ? (std::max)(_units - kUnits, kKeepBehindOverlay) : _units - kUnits;
        gl.glEnable(GL_POLYGON_OFFSET_FILL);
        gl.glPolygonOffset(factor, units);
    }

    ~ScopedSelectionDepthBias()
    {
        if (!_gl)
            return;
        if (_wasEnabled)
            _gl->glPolygonOffset(_factor, _units);
        else
            _gl->glDisable(GL_POLYGON_OFFSET_FILL);
    }

    ScopedSelectionDepthBias(const ScopedSelectionDepthBias&) = delete;
    ScopedSelectionDepthBias& operator=(const ScopedSelectionDepthBias&) = delete;

private:
    // As small as can still break an exact tie. A larger bias, above all the slope-scaled part, which grows with the surface's
    // angle to the view, pulled a highlighted panel in front of a patch that stands only a hair proud of it once the camera was far
    // enough away for one depth step to span that gap. Meshes at exactly the same place differ by at most a step or two.
    static constexpr float kFactor = 0.0f; // slope-scaled part: deliberately none
    static constexpr float kUnits = 1.0f;  // constant part, in depth-buffer resolution steps
    static constexpr float kKeepBehindOverlay = 0.5f;

    QOpenGLFunctions_4_5_Core* _gl = nullptr;
    bool _wasEnabled = false;
    float _factor = 0.0f;
    float _units = 0.0f;
};
