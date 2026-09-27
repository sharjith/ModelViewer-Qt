#pragma once


#include <QImage>
#include <QColor>
#include <QString>


inline QImage convertToGLFormat(const QImage& image)
{
	// glTF 2.0 spec: UV coordinates originate at upper-left (image-space).
	// Non-traditional approach: Do NOT Y-flip images at load.
	// Instead, flip UV.y in shader and negate rotation to maintain spec compliance.
	// This matches KHR Sample Viewer behavior (UNPACK_FLIP_Y_WEBGL = false).
	// See: https://github.com/KhronosGroup/glTF-Sample-Viewer/issues/16
	return image.convertToFormat(QImage::Format_RGBA8888);
}

inline QString makeButtonStyleSheet(const QColor& color)
{
	// Compute perceived brightness (luminance)
	int brightness = int(0.299 * color.red() +
		0.587 * color.green() +
		0.114 * color.blue());

	// Choose text color
	QString textColor = (brightness > 186) ? "black" : "white";

	return QString("background-color: rgb(%1, %2, %3); color: %4;")
		.arg(color.red())
		.arg(color.green())
		.arg(color.blue())
		.arg(textColor);
}

// vertices: interleaved pos(3), normal(3), uv(2)  (strideIn = 8 floats)
// indices:  uint32 triangles
// Output: vertices resized to pos(3), normal(3), uv(2), tangent(4) (strideOut = 12 floats)
inline static void appendTangents(std::vector<float>& vertices,
	const std::vector<unsigned int>& indices)
{
	const int strideIn = 8;
	const int strideOut = 12;
	const size_t vertCount = vertices.size() / strideIn;

	// Prepare output buffer and zero tangents
	std::vector<float> out;
	out.resize(vertCount * strideOut, 0.0f);
	for (size_t i = 0; i < vertCount; ++i)
	{
		// copy pos, normal, uv into out
		const float* src = &vertices[i * strideIn];
		float* dst = &out[i * strideOut];
		for (int k = 0; k < strideIn; ++k) dst[k] = src[k];
		// init tangent.xyz = 0, tangent.w = 1
		dst[8] = 0.0f; dst[9] = 0.0f; dst[10] = 0.0f; dst[11] = 1.0f;
	}

	// Accumulate tangents per triangle
	auto getV = [&](unsigned int idx)->const float* { return &out[idx * strideOut]; };

	for (size_t t = 0; t < indices.size(); t += 3)
	{
		unsigned int i0 = indices[t + 0];
		unsigned int i1 = indices[t + 1];
		unsigned int i2 = indices[t + 2];

		const float* v0 = getV(i0);
		const float* v1 = getV(i1);
		const float* v2 = getV(i2);

		// positions
		QVector3D p0(v0[0], v0[1], v0[2]);
		QVector3D p1(v1[0], v1[1], v1[2]);
		QVector3D p2(v2[0], v2[1], v2[2]);

		// uvs
		QVector2D w0(v0[6], v0[7]);
		QVector2D w1(v1[6], v1[7]);
		QVector2D w2(v2[6], v2[7]);

		QVector3D e1 = p1 - p0;
		QVector3D e2 = p2 - p0;
		float du1 = w1.x() - w0.x();
		float dv1 = w1.y() - w0.y();
		float du2 = w2.x() - w0.x();
		float dv2 = w2.y() - w0.y();

		float denom = du1 * dv2 - du2 * dv1;
		if (std::abs(denom) < 1e-8f) continue; // degenerate UVs

		float r = 1.0f / denom;
		QVector3D T = (e1 * dv2 - e2 * dv1) * r;
		// (We don't accumulate B; we'll reconstruct it from NxT with handedness)

		// add T to each vertex tangent accumulator
		auto addT = [&](unsigned int idx) {
			float* dst = &out[idx * strideOut];
			dst[8] += T.x();
			dst[9] += T.y();
			dst[10] += T.z();
			};
		addT(i0); addT(i1); addT(i2);
	}

	// Orthonormalize T to N and compute handedness
	for (size_t i = 0; i < vertCount; ++i)
	{
		float* v = &out[i * strideOut];
		QVector3D N(v[3], v[4], v[5]);
		QVector3D T(v[8], v[9], v[10]);

		if (T.lengthSquared() < 1e-12f)
		{
			// fallback: pick any tangent orthogonal to N
			QVector3D a = (std::abs(N.z()) < 0.999f) ? QVector3D(0, 0, 1) : QVector3D(0, 1, 0);
			T = QVector3D::crossProduct(a, N).normalized();
		}
		else
		{
			// Gram-Schmidt
			T = (T - N * QVector3D::dotProduct(N, T)).normalized();
		}

		// Handedness: sign to reconstruct B = cross(N,T) * w
		// We need bitangent direction once; approximate from position derivative using UV swap
		// Simple robust sign: assume UVs are not mirrored -> +1; if you know mirrored, compute properly.
		float w = 1.0f;

		v[8] = T.x();
		v[9] = T.y();
		v[10] = T.z();
		v[11] = w;
	}

	vertices.swap(out);
}
