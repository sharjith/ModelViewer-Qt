#include "NodeProperties.h"

#include <assimp/scene.h>
#include <QObject>
#include <cmath>
#include <cstring>

namespace
{
QString text(const aiString& value)
{
	return QString::fromUtf8(value.C_Str(), int(value.length));
}

// Display string and type label for one scalar entry; false for nested metadata (handled by the caller).
bool scalar(const aiMetadataEntry& entry, QString& value, QString& type)
{
	if (!entry.mData)
		return false;
	switch (entry.mType)
	{
	case AI_BOOL:
		value = *static_cast<const bool*>(entry.mData) ? QStringLiteral("true") : QStringLiteral("false");
		type = QStringLiteral("bool");
		return true;
	case AI_INT32:
		value = QString::number(*static_cast<const int32_t*>(entry.mData));
		type = QStringLiteral("integer");
		return true;
	case AI_UINT64:
		value = QString::number(qulonglong(*static_cast<const uint64_t*>(entry.mData)));
		type = QStringLiteral("integer");
		return true;
	case AI_INT64:
		value = QString::number(qlonglong(*static_cast<const int64_t*>(entry.mData)));
		type = QStringLiteral("integer");
		return true;
	case AI_UINT32:
		value = QString::number(*static_cast<const uint32_t*>(entry.mData));
		type = QStringLiteral("integer");
		return true;
	case AI_FLOAT:
		value = QString::number(double(*static_cast<const float*>(entry.mData)), 'g', 9);
		type = QStringLiteral("real");
		return true;
	case AI_DOUBLE:
		value = QString::number(*static_cast<const double*>(entry.mData), 'g', 17);
		type = QStringLiteral("real");
		return true;
	case AI_AISTRING:
		value = text(*static_cast<const aiString*>(entry.mData));
		type = QStringLiteral("text");
		return true;
	case AI_AIVECTOR3D:
	{
		const aiVector3D& v = *static_cast<const aiVector3D*>(entry.mData);
		value = QStringLiteral("%1, %2, %3").arg(double(v.x), 0, 'g', 9).arg(double(v.y), 0, 'g', 9).arg(double(v.z), 0, 'g', 9);
		type = QStringLiteral("vector");
		return true;
	}
	default:
		return false;
	}
}

const aiMetadata* nested(const aiMetadataEntry& entry)
{
	return entry.mType == AI_AIMETADATA ? static_cast<const aiMetadata*>(entry.mData) : nullptr;
}

const aiMetadataEntry* find(const aiMetadata& block, const char* key)
{
	for (unsigned i = 0; i < block.mNumProperties; ++i)
		if (std::strcmp(block.mKeys[i].C_Str(), key) == 0)
			return &block.mValues[i];
	return nullptr;
}
}

namespace NodeProperties
{
bool hasEntries(const aiMetadata* metadata)
{
	return metadata && metadata->mNumProperties > 0 && metadata->mKeys && metadata->mValues;
}

NodePropertySetPtr fromNode(const aiNode& node, std::shared_ptr<const PmiGeometry> pmiGeometry, bool massAggregate)
{
	if (!hasEntries(node.mMetaData))
		return nullptr;
	auto set = std::make_shared<NodePropertySet>();
	set->nodeName = text(node.mName);
	set->massAggregate = massAggregate;
	set->pmiGeometry = std::move(pmiGeometry);
	const aiMetadata& block = *node.mMetaData;
	set->rows.reserve(block.mNumProperties);
	bool truncated = false;
	auto full = [&] { return set->rows.size() >= MaxRowsPerNode; };
	for (unsigned i = 0; i < block.mNumProperties; ++i)
	{
		if (full())
		{
			truncated = true;
			break;
		}
		const QString key = text(block.mKeys[i]);
		const aiMetadataEntry& entry = block.mValues[i];
		if (const aiMetadata* group = nested(entry))
		{
			if (key == QLatin1String("JTProperties"))
			{
				// Ordered records, each with a Key and a Value entry (see JtSceneBuilder properties()).
				for (unsigned r = 0; r < group->mNumProperties; ++r)
				{
					if (full())
					{
						truncated = true;
						break;
					}
					const aiMetadata* record = nested(group->mValues[r]);
					const aiMetadataEntry* k = record ? find(*record, "Key") : nullptr;
					const aiMetadataEntry* v = record ? find(*record, "Value") : nullptr;
					QString name, value, type;
					QString ignored;
					if (!k || !v || !scalar(*k, name, ignored) || !scalar(*v, value, type))
						continue;
					set->rows.push_back({ name, value, type });
				}
			}
			else if (key == QLatin1String("JTPMI"))
			{
				// Recorded PMI (see JtSceneBuilder pmiMetadata()): one summary row per entity, then its properties.
				for (unsigned e = 0; e < group->mNumProperties; ++e)
				{
					const aiMetadata* entity = nested(group->mValues[e]);
					if (!entity)
						continue;
					QString typeName, ignored, labelText, verticesText, countText;
					QString dummyType;
					if (const aiMetadataEntry* v = find(*entity, "TypeName")) scalar(*v, typeName, dummyType);
					if (const aiMetadataEntry* v = find(*entity, "UserLabel")) scalar(*v, labelText, dummyType);
					if (const aiMetadataEntry* v = find(*entity, "PolylineVertices")) scalar(*v, verticesText, dummyType);
					if (const aiMetadataEntry* v = find(*entity, "PropertyCount")) scalar(*v, countText, dummyType);
					if (full())
					{
						truncated = true;
						break;
					}
					const QString prefix = QStringLiteral("PMI %1").arg(e);
					set->rows.push_back({ typeName.isEmpty() ? prefix : prefix + QLatin1Char(' ') + typeName,
						QObject::tr("label %1, %2 polyline vertices, %3 properties").arg(labelText, verticesText, countText),
						QStringLiteral("group") });
					// Two synthetic rows the recorded-PMI import reads: the entity type code and the 2D frame origin.
					if (const aiMetadataEntry* v = find(*entity, "EntityType"))
					{
						QString code, codeType;
						if (scalar(*v, code, codeType))
							set->rows.push_back({ prefix + QStringLiteral(".(entity type)"), code, QStringLiteral("integer") });
					}
					if (const aiMetadataEntry* v = find(*entity, "FrameOrigin"))
					{
						QString origin, originType;
						if (scalar(*v, origin, originType))
							set->rows.push_back({ prefix + QStringLiteral(".(frame origin)"), origin, QStringLiteral("text") });
					}
					const aiMetadataEntry* propertyEntry = find(*entity, "Properties");
					const aiMetadata* properties = propertyEntry ? nested(*propertyEntry) : nullptr;
					for (unsigned p = 0; properties && p < properties->mNumProperties; ++p)
					{
						if (full())
						{
							truncated = true;
							break;
						}
						const aiMetadata* record = nested(properties->mValues[p]);
						const aiMetadataEntry* k = record ? find(*record, "Key") : nullptr;
						const aiMetadataEntry* v = record ? find(*record, "Value") : nullptr;
						QString name, value, type;
						if (!k || !v || !scalar(*k, name, ignored) || !scalar(*v, value, type))
							continue;
						set->rows.push_back({ prefix + QLatin1Char('.') + name, value, type });
					}
				}
			}
			else
				set->rows.push_back({ key, QObject::tr("%1 entries").arg(group->mNumProperties), QStringLiteral("group") });
			continue;
		}
		QString value, type;
		if (scalar(entry, value, type))
			set->rows.push_back({ key, value, type });
	}
	if (truncated)
		set->rows.push_back({ QStringLiteral("..."), QObject::tr("truncated"), QStringLiteral("group") });
	if (set->rows.empty())
		return nullptr;
	return set;
}
}

namespace
{
// The CAD_MASS:: number of a property set; false when it has none or it is not a finite number >= 0.
bool massNumber(const NodePropertySet& set, double* value)
{
	for (const NodePropertyRow& row : set.rows)
	{
		if (row.key != QLatin1String("CAD_MASS::"))
			continue;
		bool ok = false;
		const double number = row.value.trimmed().toDouble(&ok);
		if (ok && std::isfinite(number) && number >= 0.0)
		{
			*value = number;
			return true;
		}
	}
	return false;
}

// The set's CAD_MASS_UNITS:: when it has one must be kilograms; no entry means kilograms (the NX files).
bool massUnitIsKilograms(const NodePropertySet& set)
{
	for (const NodePropertyRow& row : set.rows)
	{
		if (row.key != QLatin1String("CAD_MASS_UNITS::"))
			continue;
		const QString unit = row.value.trimmed().toLower();
		return unit == QLatin1String("kilograms") || unit == QLatin1String("kilogram") || unit == QLatin1String("kg");
	}
	return true;
}
}

namespace NodeProperties
{
bool declaresMassKey(const NodePropertySet& set)
{
	double ignored = 0.0;
	return massNumber(set, &ignored);
}

bool declaresMassKey(const aiNode& node)
{
	const NodePropertySetPtr set = fromNode(node);
	return set && declaresMassKey(*set);
}

DeclaredMass declaredMass(const NodePropertyChain& chain)
{
	for (const NodePropertySetPtr& set : chain)
	{
		if (!set || set->massAggregate)
			continue;
		double value = 0.0;
		if (massNumber(*set, &value) && value > 0.0 && massUnitIsKilograms(*set))
			return { value, QStringLiteral("CAD_MASS::"), set->nodeName, set.get() };
	}
	return {};
}
}

namespace NodeProperties
{
DeclaredMaterial declaredMaterial(const NodePropertyChain& chain)
{
	for (const NodePropertySetPtr& set : chain)
	{
		if (!set)
			continue;
		for (const NodePropertyRow& row : set->rows)
		{
			if (row.key != QLatin1String("STEP_MATERIAL_NAME"))
				continue;
			const QString name = row.value.trimmed();
			if (!name.isEmpty())
				return { name, row.key, set->nodeName };
		}
	}
	return {};
}
}

namespace NodeProperties
{
DeclaredPartNumber declaredPartNumber(const NodePropertyChain& chain)
{
	// Only the outermost node that has properties: the root of the product structure. A part number a sub-assembly declares is not the
	// document's, so it is never taken when the root has none.
	if (chain.empty() || !chain.back())
		return {};
	DeclaredPartNumber found;
	for (const NodePropertyRow& row : chain.back()->rows)
	{
		if (row.key == QLatin1String("3DXML_V_Name") && !row.value.trimmed().isEmpty())
		{
			found.number = row.value.trimmed();
			found.sourceKey = row.key;
		}
		else if (row.key == QLatin1String("3DXML_V_description"))
			found.description = row.value.trimmed();
	}
	return found.valid() ? found : DeclaredPartNumber();
}
}
