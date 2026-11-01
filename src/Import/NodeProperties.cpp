#include "NodeProperties.h"

#include <assimp/scene.h>
#include <QObject>
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

NodePropertySetPtr fromNode(const aiNode& node)
{
	if (!hasEntries(node.mMetaData))
		return nullptr;
	auto set = std::make_shared<NodePropertySet>();
	set->nodeName = text(node.mName);
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
