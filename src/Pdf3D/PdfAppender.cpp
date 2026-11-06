#include "PdfAppender.h"

#include <QMap>
#include <QRegularExpression>
#include <QVector>

#include <cmath>

namespace Pdf3D
{
namespace
{

struct Document
{
    const QByteArray* data = nullptr;
    qint64            xrefOffset = 0;      // the file's last xref section (becomes /Prev)
    QVector<qint64>   offsets;             // object number -> byte offset (-1 when free)
    int               size = 0;            // trailer /Size
    int               root = 0;
    int               info = 0;            // 0 when absent
    QByteArray        idArray;             // raw "[ <..> <..> ]" or empty

    // Body of object `num` (text between "N G obj" and "endobj"), empty on failure.
    QByteArray objectBody(int num) const
    {
        if (num <= 0 || num >= offsets.size() || offsets[num] < 0)
            return QByteArray();
        const qint64 start = offsets[num];
        const QByteArray header = QByteArray::number(num) + " 0 obj";
        if (data->mid(static_cast<int>(start), header.size()) != header)
            return QByteArray();
        const qint64 bodyStart = start + header.size();
        const qint64 end = data->indexOf("endobj", static_cast<int>(bodyStart));
        if (end < 0)
            return QByteArray();
        return data->mid(static_cast<int>(bodyStart), static_cast<int>(end - bodyStart)).trimmed();
    }
};

bool fail(QString* error, const QString& text)
{
    if (error)
        *error = text;
    return false;
}

bool parseDocument(const QByteArray& pdf, Document* doc, QString* error)
{
    doc->data = &pdf;
    const int startxrefPos = pdf.lastIndexOf("startxref");
    if (startxrefPos < 0)
        return fail(error, QStringLiteral("The PDF has no startxref."));

    bool ok = false;
    const QByteArray tail = pdf.mid(startxrefPos + 9, 40).trimmed();
    doc->xrefOffset = tail.left(tail.indexOf('\n') < 0 ? tail.size() : tail.indexOf('\n')).trimmed().toLongLong(&ok);
    if (!ok || doc->xrefOffset <= 0 || doc->xrefOffset >= pdf.size())
        return fail(error, QStringLiteral("The PDF's startxref offset is invalid."));

    int pos = static_cast<int>(doc->xrefOffset);
    if (pdf.mid(pos, 4) != "xref")
        return fail(error, QStringLiteral("The PDF uses a cross-reference stream; only a classic xref table is supported."));
    pos += 4;
    while (pos < pdf.size() && (pdf[pos] == '\n' || pdf[pos] == '\r' || pdf[pos] == ' '))
        ++pos;

    // subsections: "first count" then count entries of 20 bytes
    for (;;)
    {
        const int eol = pdf.indexOf('\n', pos);
        if (eol < 0)
            return fail(error, QStringLiteral("The PDF's xref table is truncated."));
        const QByteArray line = pdf.mid(pos, eol - pos).trimmed();
        if (line.startsWith("trailer"))
            break;
        const QList<QByteArray> parts = line.split(' ');
        if (parts.size() != 2)
            return fail(error, QStringLiteral("The PDF's xref table has an unexpected subsection header."));
        const int first = parts[0].toInt(&ok);
        const int count = ok ? parts[1].toInt(&ok) : 0;
        if (!ok || first < 0 || count < 0)
            return fail(error, QStringLiteral("The PDF's xref subsection header is invalid."));
        pos = eol + 1;
        if (doc->offsets.size() < first + count)
            doc->offsets.resize(first + count);
        for (int i = 0; i < count; ++i)
        {
            const QByteArray entry = pdf.mid(pos, 20);
            if (entry.size() < 18)
                return fail(error, QStringLiteral("The PDF's xref table is truncated."));
            const qint64 off = entry.left(10).toLongLong(&ok);
            doc->offsets[first + i] = (ok && entry.at(17) == 'n') ? off : -1;
            pos += 20;
        }
    }

    const int trailerStart = pdf.indexOf("<<", pos);
    const int trailerEnd = pdf.indexOf("startxref", trailerStart);
    if (trailerStart < 0 || trailerEnd < 0)
        return fail(error, QStringLiteral("The PDF's trailer is missing."));
    const QString trailer = QString::fromLatin1(pdf.mid(trailerStart, trailerEnd - trailerStart));

    QRegularExpressionMatch m = QRegularExpression(QStringLiteral("/Size\\s+(\\d+)")).match(trailer);
    if (!m.hasMatch())
        return fail(error, QStringLiteral("The PDF trailer has no /Size."));
    doc->size = m.captured(1).toInt();
    m = QRegularExpression(QStringLiteral("/Root\\s+(\\d+)\\s+0\\s+R")).match(trailer);
    if (!m.hasMatch())
        return fail(error, QStringLiteral("The PDF trailer has no /Root."));
    doc->root = m.captured(1).toInt();
    m = QRegularExpression(QStringLiteral("/Info\\s+(\\d+)\\s+0\\s+R")).match(trailer);
    doc->info = m.hasMatch() ? m.captured(1).toInt() : 0;
    m = QRegularExpression(QStringLiteral("/ID\\s*(\\[[^\\]]*\\])")).match(trailer);
    doc->idArray = m.hasMatch() ? m.captured(1).toLatin1() : QByteArray();

    if (doc->size < 1 || doc->offsets.size() < doc->size)
        doc->offsets.resize(qMax(doc->size, doc->offsets.size()));
    return true;
}

QByteArray flate(const QByteArray& raw)
{
    // qCompress() prepends a 4 byte big-endian length to a plain zlib stream; /FlateDecode wants only the zlib stream.
    QByteArray z = qCompress(raw, 9);
    z.remove(0, 4);
    return z;
}

QByteArray streamObject(const QByteArray& dictEntries, const QByteArray& raw)
{
    const QByteArray z = flate(raw);
    QByteArray out = "<< " + dictEntries + " /Filter /FlateDecode /Length " + QByteArray::number(z.size()) + " >>\nstream\n";
    out += z;
    out += "\nendstream";
    return out;
}

} // namespace

QByteArray PdfAppender::pdfNumber(double value)
{
    QByteArray t = QByteArray::number(value, 'f', 6);
    while (t.endsWith('0'))
        t.chop(1);
    if (t.endsWith('.'))
        t.chop(1);
    if (t.isEmpty() || t == "-0")
        t = "0";
    return t;
}

QByteArray PdfAppender::pdfTextString(const QString& text)
{
    bool ascii = true;
    for (const QChar c : text)
        ascii = ascii && c.unicode() >= 0x20 && c.unicode() < 0x7f;
    if (ascii)
        return pdfString(text);
    QByteArray out = "<FEFF";
    for (const QChar c : text)
        out += QByteArray::number(c.unicode(), 16).rightJustified(4, '0').toUpper();
    out += ">";
    return out;
}

QByteArray PdfAppender::pdfString(const QString& text)
{
    QByteArray out = "(";
    for (const QChar c : text)
    {
        const ushort u = c.unicode();
        if (u == '(' || u == ')' || u == '\\')
            out += '\\';
        out += (u >= 0x20 && u < 0x7f) ? static_cast<char>(u) : '?';
    }
    out += ')';
    return out;
}

bool PdfAppender::append3DAnnotation(QByteArray& pdf, const Annotation3D& a, QString* error)
{
    if (a.u3d.isEmpty() || a.viewDictionaries.isEmpty())
        return fail(error, QStringLiteral("There is no 3D data or no view to embed."));

    Document doc;
    if (!parseDocument(pdf, &doc, error))
        return false;

    // ---- find the pages: catalog -> /Pages -> /Kids (Qt writes a flat page tree) -----------------------------------------------
    const QString catalog = QString::fromLatin1(doc.objectBody(doc.root));
    QRegularExpressionMatch m = QRegularExpression(QStringLiteral("/Pages\\s+(\\d+)\\s+0\\s+R")).match(catalog);
    if (!m.hasMatch())
        return fail(error, QStringLiteral("The PDF catalog has no /Pages entry."));
    const QString pagesBody = QString::fromLatin1(doc.objectBody(m.captured(1).toInt()));
    const QRegularExpressionMatch kidsMatch = QRegularExpression(QStringLiteral("/Kids\\s*\\[([^\\]]*)\\]")).match(pagesBody);
    if (!kidsMatch.hasMatch())
        return fail(error, QStringLiteral("The PDF page tree has no pages."));
    QList<int> pageNums;
    {
        QRegularExpressionMatchIterator it = QRegularExpression(QStringLiteral("(\\d+)\\s+0\\s+R")).globalMatch(kidsMatch.captured(1));
        while (it.hasNext())
            pageNums.append(it.next().captured(1).toInt());
    }
    if (pageNums.isEmpty())
        return fail(error, QStringLiteral("The PDF page tree has no pages."));
    const int pageNum = pageNums.first();   // the 3D annotation goes on the first page
    for (const int num : pageNums)
    {
        const QString body = QString::fromLatin1(doc.objectBody(num));
        if (!body.contains(QStringLiteral("/Type /Page")) && !body.contains(QStringLiteral("/Type/Page")))
            return fail(error, QStringLiteral("The PDF page object could not be read."));
    }

    // ---- new objects --------------------------------------------------------------------------------------------------
    const int num3d = doc.size;
    const int numAnnot = doc.size + 1;
    const int numAppearance = doc.size + 2;
    const int numField = doc.size + 3;       // only used with a notes field: the field, the font, the form, then one widget per page
    const int numFont = doc.size + 4;
    const int numAcroForm = doc.size + 5;
    const int numFirstWidget = doc.size + 6;

    QByteArray views = "[";
    for (int i = 0; i < a.viewDictionaries.size(); ++i)
    {
        if (i)
            views += ' ';
        views += a.viewDictionaries[i];
    }
    views += "]";

    const double w = a.rect.width(), h = a.rect.height();
    QMap<int, QByteArray> objects;   // object number -> full body (without "N 0 obj"/"endobj"); ordered by number
    // the animation style (/AN) belongs in the 3D stream dictionary, as commercial files have it (verified in Acrobat)
    objects.insert(num3d, streamObject("/Type /3D /Subtype /U3D" + (a.animationStyle.isEmpty() ? QByteArray() : " /AN " + a.animationStyle) +
                                       " /VA " + views, a.u3d));
    // An empty appearance: the page content drawn by Qt under the annotation shows until the 3D content is activated.
    objects.insert(numAppearance, streamObject("/Type /XObject /Subtype /Form /BBox [0 0 " + pdfNumber(w) + " " + pdfNumber(h) + "]", QByteArray()));
    objects.insert(numAnnot,
        "<< /Type /Annot /Subtype /3D /P " + QByteArray::number(pageNum) + " 0 R /Rect [" + pdfNumber(a.rect.left()) + " " +
        pdfNumber(a.rect.top() > a.rect.bottom() ? a.rect.bottom() : a.rect.top()) + " " + pdfNumber(a.rect.right()) + " " +
        pdfNumber(a.rect.top() > a.rect.bottom() ? a.rect.top() : a.rect.bottom()) + "] /F 4 /Contents " + pdfString(a.description) +
        " /3DD " + QByteArray::number(num3d) + " 0 R /3DV " + QByteArray::number(a.defaultView) +
        " /3DA << /A /PO /D /PC /TB true /NP false" + (a.startLive ? " /AIS /L /DIS /I" : "") + " >> /AP << /N " + QByteArray::number(numAppearance) + " 0 R >> >>");

    // refs to add to each page's /Annots (parallel to pageNums)
    QVector<QByteArray> pageRefs(pageNums.size());
    pageRefs[0] = QByteArray::number(numAnnot) + " 0 R";
    int newObjectCount = 3;
    if (a.hasNotesField)
    {
        // The catalog is rewritten to point at a new /AcroForm; refuse files that already have one.
        if (catalog.contains(QStringLiteral("/AcroForm")))
            return fail(error, QStringLiteral("The PDF already has an interactive form."));
        QByteArray utf16 = "<FEFF";
        for (const QChar c : a.notesField.text)
            utf16 += QByteArray::number(c.unicode(), 16).rightJustified(4, '0').toUpper();
        utf16 += ">";
        const QByteArray da = "(/Helv " + pdfNumber(a.notesField.fontSize) + " Tf 0 g)";
        const QRectF r = a.notesField.rect;
        const double lo = qMin(r.top(), r.bottom()), hi = qMax(r.top(), r.bottom());
        const QByteArray rect = "[" + pdfNumber(r.left()) + " " + pdfNumber(lo) + " " + pdfNumber(r.right()) + " " + pdfNumber(hi) + "]";

        // one field, one widget on every page (same rectangle: the title block is the same on every sheet); the widgets share the
        // field's value, so scrolling or editing shows the same text everywhere
        QByteArray kids;
        for (int i = 0; i < pageNums.size(); ++i)
        {
            const int widget = numFirstWidget + i;
            if (i)
                kids += ' ';
            kids += QByteArray::number(widget) + " 0 R";
            objects.insert(widget, "<< /Type /Annot /Subtype /Widget /Parent " + QByteArray::number(numField) + " 0 R /P " +
                QByteArray::number(pageNums[i]) + " 0 R /Rect " + rect + " /F 4 /MK << /BG [1 1 1] >> >>");
            if (!pageRefs[i].isEmpty())
                pageRefs[i] += ' ';
            pageRefs[i] += QByteArray::number(widget) + " 0 R";
        }
        objects.insert(numField, "<< /FT /Tx /T (Notes) /TU (Notes) /Ff 4198400 /V " + utf16 + " /DA " + da + " /Kids [" + kids + "] >>");
        objects.insert(numFont, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
        objects.insert(numAcroForm,
            "<< /Fields [" + QByteArray::number(numField) + " 0 R] /NeedAppearances true /DA " + da + " /DR << /Font << /Helv " +
            QByteArray::number(numFont) + " 0 R >> >> >>");
        QByteArray cat = doc.objectBody(doc.root);
        const int end = cat.lastIndexOf(">>");
        if (end < 0)
            return fail(error, QStringLiteral("The PDF catalog could not be rewritten."));
        cat.insert(end, "/AcroForm " + QByteArray::number(numAcroForm) + " 0 R\n");
        objects.insert(doc.root, cat);
        newObjectCount = 6 + static_cast<int>(pageNums.size());
    }

    // ---- hook the new annotations into each page's /Annots ------------------------------------------------------------------
    for (int pi = 0; pi < pageNums.size(); ++pi)
    {
        if (pageRefs[pi].isEmpty())
            continue;
        const int pageObj = pageNums[pi];
        const QByteArray newRef = pageRefs[pi];
        const QByteArray pageBodyBytes = doc.objectBody(pageObj);
        const QString pageBody = QString::fromLatin1(pageBodyBytes);
        QRegularExpressionMatch annotsRef = QRegularExpression(QStringLiteral("/Annots\\s+(\\d+)\\s+0\\s+R")).match(pageBody);
        if (annotsRef.hasMatch())
        {
            const int arrayNum = annotsRef.captured(1).toInt();
            QByteArray arr = doc.objectBody(arrayNum);
            const int open = arr.indexOf('['), close = arr.lastIndexOf(']');
            if (open < 0 || close < open)
                return fail(error, QStringLiteral("The page's /Annots array could not be read."));
            const QByteArray inner = arr.mid(open + 1, close - open - 1).trimmed();
            objects.insert(arrayNum, "[ " + (inner.isEmpty() ? QByteArray() : inner + " ") + newRef + " ]");
        }
        else
        {
            // inline /Annots [...] or none: rewrite the page object itself
            QByteArray page = pageBodyBytes;
            QRegularExpressionMatch inlineAnnots = QRegularExpression(QStringLiteral("/Annots\\s*\\[([^\\]]*)\\]")).match(pageBody);
            if (inlineAnnots.hasMatch())
            {
                const QString replaced = QString(pageBody).replace(inlineAnnots.capturedStart(0), inlineAnnots.capturedLength(0),
                    QStringLiteral("/Annots [%1 %2]").arg(inlineAnnots.captured(1).trimmed(), QString::fromLatin1(newRef)));
                page = replaced.toLatin1();
            }
            else
            {
                const int end = page.lastIndexOf(">>");
                if (end < 0)
                    return fail(error, QStringLiteral("The page object could not be rewritten."));
                page.insert(end, "/Annots [" + newRef + "]\n");
            }
            objects.insert(pageObj, page);
        }
    }

    // ---- append: objects, xref section, trailer ---------------------------------------------------------------------------
    QByteArray out = pdf;
    if (!out.endsWith('\n'))
        out += '\n';
    QMap<int, qint64> newOffsets;
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it)
    {
        newOffsets.insert(it.key(), out.size());
        out += QByteArray::number(it.key()) + " 0 obj\n" + it.value() + "\nendobj\n";
    }

    const qint64 xrefPos = out.size();
    out += "xref\n";
    for (auto it = newOffsets.constBegin(); it != newOffsets.constEnd(); ++it)
    {
        out += QByteArray::number(it.key()) + " 1\n";
        out += QByteArray::number(it.value()).rightJustified(10, '0') + " 00000 n \n";
    }
    const int newSize = qMax(doc.size, doc.size + newObjectCount);
    out += "trailer\n<< /Size " + QByteArray::number(newSize) + " /Root " + QByteArray::number(doc.root) + " 0 R";
    if (doc.info > 0)
        out += " /Info " + QByteArray::number(doc.info) + " 0 R";
    if (!doc.idArray.isEmpty())
        out += " /ID " + doc.idArray;
    out += " /Prev " + QByteArray::number(doc.xrefOffset) + " >>\nstartxref\n" + QByteArray::number(xrefPos) + "\n%%EOF\n";

    pdf = out;
    return true;
}

} // namespace Pdf3D
