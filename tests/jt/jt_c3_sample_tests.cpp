// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Regression checks of the NX polyline / point set decoder (C3 items 1-2) against the owner's authentic JT10.10 LE samples.
// The samples are local only (never in Git): set MV_JT_C3_SAMPLES to their folder, or keep them in
// out/jt-research/c3-nx-samples. A missing folder or file skips that check with a message; nothing is fabricated.
// The expected counts are the ones recorded in research/jt/JT_C3_POLYLINE_POINT_DECODER.md (they equal the declared Wireframe Rep
// edge counts); the owner confirmed the geometry visually against another viewer on 5 November 2026.
#include "Jt/JtImporter.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool value, const std::string& why) { if (!value) throw std::runtime_error(why); }

struct Expected
{
    const char* file;
    int lines;     // line-segment faces over all meshes
    int points;    // point faces over all meshes, -1 = not asserted
    bool triangles;  // the file also carries a triangle solid (none of the NX samples does, C3_M1 included)
};

// Segments + points per sample as listed in the decoder note (jt_probe, default options).
constexpr Expected kSamples[] = {
    {"C3_P1.jt", 4, 5, false},       {"C3_P2.jt", 5, 7, false},  {"C3_P3.jt", 7, 8, false},
    {"C3_M1.jt", 5, 7, false},       {"C3_PT1.jt", 0, 6, false}, {"C3_C1.jt", 5, -1, false},
    {"C3_U1.jt", 2, -1, false},      {"C3_V1.jt", 2, -1, false}, {"polyline1.jt", 4, 5, false},
    {"polyline2.jt", 3, 4, false},
};

QString sampleFolder()
{
    const QString env = qEnvironmentVariable("MV_JT_C3_SAMPLES");
    if (!env.isEmpty())
        return env;
    // tests/jt/<this file> -> repository root
    return QDir(QFileInfo(QStringLiteral(__FILE__)).absolutePath() + QStringLiteral("/../.."))
        .absoluteFilePath(QStringLiteral("out/jt-research/c3-nx-samples"));
}

void walk(const aiNode* node, const aiScene& scene, int& lines, int& points, int& triangles)
{
    for (unsigned i = 0; i < node->mNumMeshes; ++i)
    {
        const aiMesh& mesh = *scene.mMeshes[node->mMeshes[i]];
        unsigned expectedIndices = 0;
        if (mesh.mPrimitiveTypes == aiPrimitiveType_LINE) expectedIndices = 2;
        else if (mesh.mPrimitiveTypes == aiPrimitiveType_POINT) expectedIndices = 1;
        else if (mesh.mPrimitiveTypes == aiPrimitiveType_TRIANGLE) expectedIndices = 3;
        check(expectedIndices != 0, "a mesh mixes primitive kinds or has none");
        for (unsigned f = 0; f < mesh.mNumFaces; ++f)
        {
            check(mesh.mFaces[f].mNumIndices == expectedIndices, "face index count does not match the primitive kind");
            for (unsigned k = 0; k < expectedIndices; ++k)
                check(mesh.mFaces[f].mIndices[k] < mesh.mNumVertices, "face index out of range");
        }
        if (expectedIndices == 2) lines += int(mesh.mNumFaces);
        else if (expectedIndices == 1) points += int(mesh.mNumFaces);
        else triangles += int(mesh.mNumFaces);
    }
    for (unsigned i = 0; i < node->mNumChildren; ++i)
        walk(node->mChildren[i], scene, lines, points, triangles);
}
}

void c3SampleTests()
{
    const QString folder = sampleFolder();
    if (!QDir(folder).exists())
    {
        std::cout << "c3 samples: skipped (no sample folder; set MV_JT_C3_SAMPLES)\n";
        return;
    }
    int ran = 0;
    for (const Expected& sample : kSamples)
    {
        const QString path = QDir(folder).absoluteFilePath(QString::fromLatin1(sample.file));
        if (!QFileInfo::exists(path))
        {
            std::cout << "c3 samples: " << sample.file << " skipped (file not present)\n";
            continue;
        }
        const auto result = Jt::Importer::read(path);
        check(result.scene && result.scene->mRootNode, std::string(sample.file) + ": no scene");
        int lines = 0, points = 0, triangles = 0;
        walk(result.scene->mRootNode, *result.scene, lines, points, triangles);
        check(lines == sample.lines, std::string(sample.file) + ": unexpected line-segment count " + std::to_string(lines));
        if (sample.points >= 0)
            check(points == sample.points, std::string(sample.file) + ": unexpected point count " + std::to_string(points));
        if (sample.triangles)
            check(triangles > 0, std::string(sample.file) + ": the triangle solid was dropped");
        else
            check(triangles == 0, std::string(sample.file) + ": triangles were created for a wire-only file");
        ++ran;
    }
    std::cout << "c3 samples: " << ran << " of " << (sizeof(kSamples) / sizeof(kSamples[0])) << " files checked\n";
}
