// Headless tests for the MVF loader's rules that need no GL context.
#include "MvfIndexRule.h"

#include <cstdio>

namespace
{
	int checks = 0, failures = 0;
	#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (false)

	QJsonObject accessor(int count)
	{
		QJsonObject a;
		a.insert(QStringLiteral("count"), count);
		return a;
	}

	QJsonObject primitive(int mode, int indices = -2)
	{
		QJsonObject p;
		if (mode >= 0)
			p.insert(QStringLiteral("mode"), mode);
		if (indices != -2)
			p.insert(QStringLiteral("indices"), indices);
		return p;
	}

	void testUnindexedRule()
	{
		const QJsonArray accessors{ accessor(3), accessor(0), accessor(12) };
		constexpr int points = 0, lines = 1, lineLoop = 2, lineStrip = 3, triangles = 4;

		// Point / line primitives with no index accessor, or an explicit zero-count one (what our writer emits): kept.
		for (int mode : { points, lines, lineLoop, lineStrip })
		{
			CHECK(Mvf::primitiveMayBeUnindexed(primitive(mode), accessors));
			CHECK(Mvf::primitiveMayBeUnindexed(primitive(mode, 1), accessors));
		}

		// A named accessor that holds elements but failed to read is damage, as is a missing / negative / out-of-range one.
		CHECK(!Mvf::primitiveMayBeUnindexed(primitive(points, 2), accessors));
		CHECK(!Mvf::primitiveMayBeUnindexed(primitive(points, -1), accessors));
		CHECK(!Mvf::primitiveMayBeUnindexed(primitive(lines, 3), accessors));
		CHECK(!Mvf::primitiveMayBeUnindexed(primitive(lines, 99), accessors));

		// Accessor without a count is not "explicitly zero".
		CHECK(!Mvf::primitiveMayBeUnindexed(primitive(points, 0), QJsonArray{ QJsonObject() }));

		// Triangles always need indices, with or without an accessor, and a missing mode means triangles.
		CHECK(!Mvf::primitiveMayBeUnindexed(primitive(triangles), accessors));
		CHECK(!Mvf::primitiveMayBeUnindexed(primitive(triangles, 1), accessors));
		CHECK(!Mvf::primitiveMayBeUnindexed(primitive(-1), accessors));
		CHECK(!Mvf::primitiveMayBeUnindexed(primitive(5), accessors)); // triangle strip
		CHECK(!Mvf::primitiveMayBeUnindexed(primitive(6), accessors)); // triangle fan
	}
}

int main()
{
	testUnindexedRule();
	std::printf("%d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
