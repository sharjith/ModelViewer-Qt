#pragma once

#include "Pdf3DViews.h"
#include "U3dScene.h"

#include <QImage>
#include <QMap>
#include <QSet>
#include <QString>

// Save as 3D PDF: an A4 landscape drawing sheet (border, title block, 3D area), optionally followed by parts list sheets, written with QPdfWriter,
// with the model embedded as a Universal 3D stream in a 3D annotation (appended as a PDF incremental update, see
// PdfAppender.h) and the standard named views. Design and decisions: research/3dpdf/3D_PDF_EXPORT_RESEARCH.md, section 9.
namespace Pdf3D
{

// How the animation of the model (U3dScene::animation) plays in Acrobat.
enum class AnimationPlayback
{
    Loop,           // again and again from the start
    BackAndForth,   // forwards, then backwards, again and again
    Once            // once
};

struct Pdf3DOptions
{
    QString title;                     // sheet title (title block, PDF document title)
    QString subtitle;                  // second title block line, e.g. the source file name
    QString author;                    // title block fields; empty ones are left blank on the sheet
    QString company;
    QString drawingNumber;
    QString revision;
    QString scale;                     // free text, e.g. "1:2" or "NTS"
    QString notes;                     // short free text under the subtitle
    QString date;                      // empty = today (ISO)
    QString templateId = QStringLiteral("builtin:default");   // the sheet template (SheetTemplate.h); an unknown id falls back to Default
    QMap<QString, QString> fieldValues;   // values of the template's custom fields, by field id
    QVector<CameraViewSpec> extraViews; // saved camera views, scene states and exploded views, added after the standard views
    QSet<QString> excludedStandardViews; // names (standardViewNames()) of standard views left out of the PDF
    QSet<QString> standardViewsWithoutOverlays; // standard views that hide the measurement and annotation nodes
    AnimationPlayback animationPlayback = AnimationPlayback::Loop;
    bool    animationAutoStart = true;   // start the animation when the 3D content opens (the 3D area is live on opening)
    bool    viewButtons = true;        // a strip of buttons under the 3D area: previous/next view, one per view when they fit, play/pause
    bool    includePartsList = true;   // extra sheet(s) listing the exported parts (item, name, material, quantity)
    UpAxis  upAxis = UpAxis::Y;        // the viewer's world up axis; decides what Front/Top/... mean
    QImage  poster;                    // optional picture shown on the page where 3D is not available (a viewport grab)
};

struct Pdf3DResult
{
    bool    ok = false;
    QString error;
};

class Pdf3DExporter
{
public:
    // Writes `path`. The scene must contain geometry (U3dScene::isEmpty() false). Not re-entrant, see U3dWriter.
    static Pdf3DResult exportToFile(const U3dScene& scene, const Pdf3DOptions& options, const QString& path);
};

} // namespace Pdf3D
