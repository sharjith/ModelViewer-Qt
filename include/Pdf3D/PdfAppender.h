#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QRectF>
#include <QString>

// Appends a 3D annotation to a one-page PDF written by QPdfWriter, as a PDF incremental update (new objects, a new xref
// section and a trailer with /Prev; the original bytes are not touched). Qt writes a classic xref table, a flat page tree and an
// indirect /Annots array for every page (research doc section 9.1); anything else (xref streams, object streams) is refused
// with an error instead of corrupting the file.
namespace Pdf3D
{

struct Annotation3D
{
    QRectF             rect;                 // in PDF points, origin at the bottom left of the page
    QByteArray         u3d;                  // the .u3d stream
    QList<QByteArray>  viewDictionaries;     // complete "<< /Type /3DView ... >>" dictionaries (see Pdf3DViews.h)
    int                defaultView = 0;      // index into viewDictionaries; written as /3DV
    QString            description;          // /Contents of the annotation (alternate text), ASCII
};

class PdfAppender
{
public:
    // Appends the annotation to `pdf` in place. Returns false and sets *error when the file is not in the expected form.
    static bool append3DAnnotation(QByteArray& pdf, const Annotation3D& annotation, QString* error);

    // PDF literal string for ASCII text ("(...)" with the special characters escaped; non-ASCII becomes '?').
    static QByteArray pdfString(const QString& text);

    // PDF number without exponent notation.
    static QByteArray pdfNumber(double value);
};

} // namespace Pdf3D
