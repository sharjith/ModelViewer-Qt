#include "ProductStructureReader.h"
#include "ArchiveReader.h"
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <cmath>

namespace ThreeDXml {
namespace {
void finish(QXmlStreamReader& xml) {
    if (xml.hasError()) throw Error(QStringLiteral("3DXML XML error at line %1: %2").arg(xml.lineNumber()).arg(xml.errorString()));
}
void checkToken(QXmlStreamReader& xml) {
    if (xml.isDTD() || xml.isEntityReference()) throw Error(QStringLiteral("Unsupported XML DTD/entity"));
}
}
QString ProductStructureReader::rootMember(const QByteArray& manifest)
{
    QXmlStreamReader xml(manifest);
    QString root;
    while (!xml.atEnd()) {
        xml.readNext(); checkToken(xml);
        if (xml.isStartElement() && xml.name() == QLatin1String("Root")) {
            if (!root.isEmpty()) throw Error(QStringLiteral("Multiple manifest roots"));
            root = ArchiveReader::memberPath(xml.readElementText().trimmed());
        }
    }
    finish(xml);
    if (root.isEmpty()) throw Error(QStringLiteral("Missing 3DXML manifest root"));
    return root;
}

Document ProductStructureReader::read(const QByteArray& bytes, const Options& options)
{
    QXmlStreamReader xml(bytes);
    Document doc;
    QSet<QString> ids;
    struct RepLink { QString parent, target; };
    std::vector<RepLink> links;
    auto identity = [&](const QString& id) {
        if (id.isEmpty() || ids.contains(id) || ids.size() >= 100000)
            throw Error(QStringLiteral("Missing/duplicate/excessive product ID: %1").arg(id));
        ids.insert(id);
    };
    while (!xml.atEnd()) {
        options.check(); xml.readNext(); checkToken(xml);
        if (!xml.isStartElement()) continue;
        const QString tag = xml.name().toString();
        const auto attrs = xml.attributes();
        if (tag == QLatin1String("ProductStructure")) {
            if (!doc.root.isEmpty()) throw Error(QStringLiteral("Multiple product structures"));
            doc.root = attrs.value("root").toString();
        } else if (tag == QLatin1String("Reference3D")) {
            Reference ref; ref.id = attrs.value("id").toString(); ref.name = attrs.value("name").toString();
            identity(ref.id); doc.references.insert(ref.id, ref);
        } else if (tag == QLatin1String("ReferenceRep")) {
            Representation rep;
            rep.id = attrs.value("id").toString(); rep.name = attrs.value("name").toString();
            rep.format = attrs.value("format").toString();
            rep.member = ArchiveReader::memberPath(attrs.value("associatedFile").toString());
            identity(rep.id); doc.representations.insert(rep.id, rep);
        } else if (tag == QLatin1String("Instance3D") || tag == QLatin1String("InstanceRep")) {
            Instance instance; instance.id = attrs.value("id").toString(); instance.name = attrs.value("name").toString();
            identity(instance.id);
            QHash<QString, QString> fields;
            while (xml.readNextStartElement()) fields.insert(xml.name().toString(), xml.readElementText().trimmed());
            instance.parent = fields.value("IsAggregatedBy"); instance.reference = fields.value("IsInstanceOf");
            if (tag == QLatin1String("InstanceRep")) links.push_back({instance.parent, instance.reference});
            else {
                const auto values = fields.value("RelativeMatrix").split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
                if (values.size() != 12) throw Error(QStringLiteral("Invalid RelativeMatrix on %1").arg(instance.id));
                ai_real* matrix = &instance.local.a1;
                for (int k = 0; k < 12; ++k) {
                    bool ok = false; double v = values[k].toDouble(&ok);
                    if (!ok || !std::isfinite(v) || !std::isfinite(static_cast<ai_real>(v))) throw Error(QStringLiteral("Nonfinite matrix"));
                    matrix[(k < 9 ? k%3 : k-9)*4 + (k < 9 ? k/3 : 3)] = static_cast<ai_real>(v);
                }
                doc.instances.insert(instance.id, instance);
                // Append after parsing all definitions; XML declaration order is not significant.
            }
        } else if (tag == QLatin1String("CATMatConnection")) {
            // A material dressing (covering) the occurrence addressed by a root-to-leaf instance path. MadeOf (core)
            // links and other roles are ignored: the published guide prefers the covering material for rendering.
            QString role, materialDocument, materialReference, path; QStringList ids; int layer = 0, depth = 1;
            while (depth && !xml.atEnd()) {
                options.check(); xml.readNext(); checkToken(xml);
                if (xml.isStartElement()) {
                    ++depth; const auto name = xml.name();
                    if (name == QLatin1String("PLMRelation")) { role.clear(); ids.clear(); }
                    else if (name == QLatin1String("C_Role")) { role = xml.readElementText().trimmed(); --depth; }
                    else if (name == QLatin1String("id")) { ids.append(xml.readElementText().trimmed()); --depth; }
                    else if (name == QLatin1String("V_Layer")) { layer = xml.readElementText().trimmed().toInt(); --depth; }
                } else if (xml.isEndElement()) {
                    if (xml.name() == QLatin1String("PLMRelation")) {
                        if (role == QLatin1String("CATMaterialToReferenceLink") && ids.size() == 1 && ids.front().contains('#')) {
                            materialDocument = ArchiveReader::memberPath(ids.front().section('#', 0, 0));
                            materialReference = ids.front().section('#', -1);
                        } else if (role == QLatin1String("CATMaterialDressByLink") && !ids.isEmpty()) {
                            QStringList parts; for (const auto& id : ids) parts.append(id.section('#', -1));
                            path = parts.join('/');
                        }
                    }
                    --depth;
                }
            }
            if (!materialDocument.isEmpty() && !materialReference.isEmpty() && !path.isEmpty())
                doc.materialLinks.push_back({materialDocument, materialReference, path, layer});
        } else if (tag == QLatin1String("DefaultViewProperty")) {
            QStringList path; bool hidden = false, hasColor = false; aiColor4D color; int depth = 1, surfaceDepth = -1;
            while (depth && !xml.atEnd()) {
                xml.readNext(); checkToken(xml);
                if (xml.isStartElement()) {
                    ++depth;
                    if (xml.name() == QLatin1String("SurfaceAttributes")) surfaceDepth = depth;
                    if (xml.name() == QLatin1String("Color") && depth == surfaceDepth+1 && surfaceDepth > 0) {
                        if (hasColor) throw Error(QStringLiteral("Multiple colors in occurrence property"));
                        const auto attributes = xml.attributes();
                        const char* channels[] = {"red","green","blue","alpha"};
                        bool unset=false;
                        for (int k=0;k<4;++k) {
                            if (k==3 && !attributes.hasAttribute(QLatin1String("alpha"))) { color[k]=1; continue; }
                            bool ok=false; const double value=attributes.value(QLatin1String(channels[k])).toDouble(&ok);
                            if (!ok || !std::isfinite(value) || value>1) throw Error(QStringLiteral("Invalid occurrence color"));
                            // The published guide defines channels as 0..1. CATIA V5 writes -1 for an occurrence with no user
                            // colour (the owner confirmed the sample part has none); treated as "no override".
                            if (value<0) { if (k<3) unset=true; color[k]=1; continue; }
                            color[k]=static_cast<ai_real>(value);
                        }
                        hasColor=!unset;
                    }
                    if (xml.name() == QLatin1String("GeneralAttributes") && xml.attributes().value("visible") == QLatin1String("false")) hidden = true;
                    if (xml.name() == QLatin1String("id")) {
                        path.append(xml.readElementText().trimmed().section('#', -1)); --depth;
                    }
                } else if (xml.isEndElement()) { if (depth==surfaceDepth) surfaceDepth=-1; --depth; }
            }
            if (hasColor) {
                if (path.isEmpty()) throw Error(QStringLiteral("Color property has no occurrence path"));
                const auto key=path.join('/');
                if (doc.occurrenceColors.contains(key)) throw Error(QStringLiteral("Duplicate occurrence color property"));
                doc.occurrenceColors.insert(key,color);
            }
            if (hidden) {
                if (path.isEmpty()) throw Error(QStringLiteral("Visibility property has no occurrence path"));
                doc.hiddenPaths.insert(path.join('/'));
            }
        }
    }
    finish(xml);
    if (doc.root.isEmpty()) throw Error(QStringLiteral("This XML document has no product structure. Material/image documents are companion resources; open the assembly document named in Manifest.xml."));
    if (!doc.references.contains(doc.root)) throw Error(QStringLiteral("Missing product root reference"));
    // QHash iteration is not declaration order; retain XML order for occurrence DFS.
    QXmlStreamReader ordered(bytes);
    while (!ordered.atEnd()) {
        ordered.readNext();
        if (ordered.isStartElement() && ordered.name() == QLatin1String("Instance3D")) {
            const auto item = doc.instances.value(ordered.attributes().value("id").toString());
            if (!doc.references.contains(item.parent) || !doc.references.contains(item.reference))
                throw Error(QStringLiteral("Unresolved Instance3D reference: %1").arg(item.id));
            doc.references[item.parent].instances.append(item.id);
        }
    }
    for (const auto& link : links) {
        if (!doc.references.contains(link.parent) || !doc.representations.contains(link.target))
            throw Error(QStringLiteral("Unresolved InstanceRep reference"));
        doc.references[link.parent].representations.append(link.target);
    }
    return doc;
}

QHash<QString, QString> ProductStructureReader::materialRenderingMembers(const QString& document, const QByteArray& bytes)
{
    QXmlStreamReader xml(bytes);
    QHash<QString, QString> domainMembers, result; // domain id -> rendering member; "<document>#<reference>" -> member
    struct Instance { QString reference, domain; };
    std::vector<Instance> instances;
    while (!xml.atEnd()) {
        xml.readNext(); checkToken(xml);
        if (!xml.isStartElement()) continue;
        const QString tag = xml.name().toString();
        if (tag == QLatin1String("MaterialDomain")) {
            const QString id = xml.attributes().value("id").toString(), file = xml.attributes().value("associatedFile").toString();
            QHash<QString, QString> fields;
            while (xml.readNextStartElement()) fields.insert(xml.name().toString(), xml.readElementText().trimmed());
            if (!id.isEmpty() && !file.isEmpty() && fields.value("V_MatDomain") == QLatin1String("Rendering"))
                domainMembers.insert(id, ArchiveReader::memberPath(file));
        } else if (tag == QLatin1String("MaterialDomainInstance")) {
            QHash<QString, QString> fields;
            while (xml.readNextStartElement()) fields.insert(xml.name().toString(), xml.readElementText().trimmed());
            instances.push_back({fields.value("IsAggregatedBy"), fields.value("IsInstanceOf")});
        }
    }
    finish(xml);
    for (const auto& instance : instances)
        if (domainMembers.contains(instance.domain)) result.insert(document + QLatin1Char('#') + instance.reference, domainMembers.value(instance.domain));
    return result;
}

MaterialRendering ProductStructureReader::materialRendering(const QByteArray& bytes)
{
    QXmlStreamReader xml(bytes);
    MaterialRendering m; bool found = false, inFeature = false;
    auto numbers = [](QString value) {
        value.remove('[').remove(']'); QList<double> out;
        for (const auto& part : value.split(',', Qt::SkipEmptyParts)) {
            bool ok = false; const double v = part.trimmed().toDouble(&ok);
            if (!ok || !std::isfinite(v)) throw Error(QStringLiteral("Invalid material rendering value"));
            out.append(v);
        }
        return out;
    };
    auto triple = [&](const QString& value) {
        const auto v = numbers(value); if (v.size() != 3) throw Error(QStringLiteral("Invalid material rendering color"));
        return aiColor3D(float(v[0]), float(v[1]), float(v[2]));
    };
    auto scalar = [&](const QString& value) {
        const auto v = numbers(value); if (v.size() != 1) throw Error(QStringLiteral("Invalid material rendering value"));
        return v[0];
    };
    while (!xml.atEnd()) {
        xml.readNext(); checkToken(xml);
        if (xml.isStartElement() && xml.name() == QLatin1String("Feature")) inFeature = xml.attributes().value("StartUp") == QLatin1String("RenderingFeature");
        else if (xml.isEndElement() && xml.name() == QLatin1String("Feature")) inFeature = false;
        else if (inFeature && xml.isStartElement() && xml.name() == QLatin1String("Attr")) {
            const QString name = xml.attributes().value("Name").toString(), value = xml.attributes().value("Value").toString();
            found = true;
            if (name == QLatin1String("DiffuseColor")) m.diffuse = triple(value);
            else if (name == QLatin1String("DiffuseCoef")) m.diffuseCoef = scalar(value);
            else if (name == QLatin1String("SpecularColor")) m.specular = triple(value);
            else if (name == QLatin1String("SpecularCoef")) m.specularCoef = scalar(value);
            else if (name == QLatin1String("SpecularExponent")) m.specularExponent = scalar(value);
            else if (name == QLatin1String("EmissiveColor")) m.emissive = triple(value);
            else if (name == QLatin1String("EmissiveCoef")) m.emissiveCoef = scalar(value);
            else if (name == QLatin1String("Transparency")) m.transparency = scalar(value);
        }
    }
    finish(xml);
    if (!found) throw Error(QStringLiteral("Material rendering has no RenderingFeature"));
    return m;
}
}
