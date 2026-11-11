#pragma once

#include "U3dScene.h"

#include <QSet>
#include <QString>
#include <QStringList>

namespace Pdf3D
{

// What the exported parts declare about their material and mass, boiled down for the sheet's {material} and {mass} values.
// The numbers are only ever the ones the source files declared (JT "CAD_MASS::", the STEP material name); nothing is estimated here and a part
// that declares nothing contributes nothing. See research/materials/MATERIAL_AND_MASS_SOURCES.md.
struct PartSummary
{
    int         parts = 0;           // exported parts (overlay nodes excluded)
    int         withMaterial = 0;    // parts that declare a material designation
    int         withMass = 0;        // parts that declare a mass
    QStringList materials;           // the distinct designations, in order of first appearance
    double      massKg = 0.0;        // the declared masses added up, each part counted once (see U3dNode::declaredMassGroup)
    QString     material;            // the sheet text; empty when no part declares a material
    QString     mass;                // the sheet text; empty when no part declares a mass
};

// Counts mesh occurrences and adds each declared mass group once.
class PartSummaryBuilder
{
public:
    void addPart(const QString& material, double massKg, const QString& massGroup);
    PartSummary summary() const;

private:
    PartSummary _counts;
    QSet<QString> _countedMasses;
};

PartSummary summarisePartData(const U3dScene& scene);

// The viewer's render material name as a parts list "Material" entry. The importers name a material after the part ("<file base name>
// (<part>)", with " (vertex colours)" added to a colour variant) and unnamed ones become "Material<n>": those are not materials, so they give an
// empty string. A name somebody chose (a library preset such as "Steel") is kept.
QString renderMaterialLabel(const QString& name, const QStringList& fileBaseNames);

// "418.1 kg"; masses under one gram are shown in grams ("0.4748 g").
QString formatMass(double kilograms);

} // namespace Pdf3D
