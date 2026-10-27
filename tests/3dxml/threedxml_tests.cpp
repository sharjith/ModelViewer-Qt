#include "Importer.h"
#include "ArchiveReader.h"
#include "ProductStructureReader.h"
#include "RepresentationReader.h"
#include "TessellatedReader.h"
#include "PackedReader.h"
#include "../../include/Import/SceneImportCounts.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <assimp/material.h>
#include <minizip/zip.h>
#include <QtEndian>
#include <iostream>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

using namespace ThreeDXml;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class Fn> void rejects(Fn fn) { bool rejected=false; try { fn(); } catch (const Error&) { rejected=true; } require(rejected,"expected checked rejection"); }
QByteArray readFile(const QString& path) { QFile f(path); require(f.open(QIODevice::ReadOnly),"cannot open reference file"); return f.readAll(); }
// Mesh names read "<part name> [<representation member>__body_N]"; the bracketed source id is what the references are keyed by.
QString sourceMeshName(const aiMesh& mesh)
{
    const QString name=QString::fromUtf8(mesh.mName.C_Str()); const int open=name.lastIndexOf('[');
    return open>=0&&name.endsWith(']') ? name.mid(open+1,name.size()-open-2) : name;
}
void writeZip(const QString& path,const std::vector<std::pair<QByteArray,QByteArray>>& entries)
{
    zipFile zip=zipOpen64(QFile::encodeName(path).constData(),APPEND_STATUS_CREATE);
    require(zip!=nullptr,"create ZIP fixture");
    for(const auto& entry:entries) {
        require(zipOpenNewFileInZip(zip,entry.first.constData(),nullptr,nullptr,0,nullptr,0,nullptr,0,0)==ZIP_OK,"write ZIP header");
        require(zipWriteInFileInZip(zip,entry.second.constData(),entry.second.size())==ZIP_OK,"write ZIP payload");
        require(zipCloseFileInZip(zip)==ZIP_OK,"close ZIP payload");
    }
    require(zipClose(zip,nullptr)==ZIP_OK,"close ZIP fixture");
}


QByteArray binaryFixture(const QByteArray& skeleton) {
    auto integer=[](quint32 v) { QByteArray b(4,'\0'); qToBigEndian(v,reinterpret_cast<uchar*>(b.data())); return b; };
    auto shortInteger=[](quint16 v) { QByteArray b(2,'\0'); qToBigEndian(v,reinterpret_cast<uchar*>(b.data())); return b; };
    const QByteArray name=QByteArray::fromHex("0053006b0065006c00650074006f006e0000");
    QByteArray catalog=integer(12+14+name.size())+integer(2)+integer(1);
    catalog+=integer(14+name.size())+integer(3)+shortInteger(2)+integer(name.size())+name;
    const quint32 streamAt=128+54+4+name.size()+54;
    const quint32 catalogAt=streamAt+skeleton.size();
    auto descriptor=[&](quint32 id,quint16 kind,quint32 physical,quint32 size) {
        return integer(54)+integer(id)+shortInteger(kind)+integer(kind)+integer(size)+integer(0)+QByteArray(8,'\0')+integer(1)+integer(physical)+integer(size)+integer(size)+integer(0)+integer(0);
    };
    QByteArray data(128,'\0'); data.replace(0,8,QByteArray("V5_CFV3\0",8));
    data.replace(8,4,integer(catalogAt+catalog.size())); data.replace(89,2,shortInteger(3));
    data+=descriptor(2,1,catalogAt,catalog.size());
    data+=integer(name.size())+name+descriptor(3,2,streamAt,skeleton.size());
    data+=skeleton+catalog; return data;
}
void synthetic()
{

    const QByteArray emptyGroup=QByteArray(1,char(0x62))+QByteArray(35,'\0')+QByteArray::fromHex("ff03010000ff00000000");
    const QByteArray extended=QByteArray::fromHex("a3ff35000000")+emptyGroup.mid(1)+QByteArray::fromHex("0000803f");
    require(RepresentationReader::read(binaryFixture(extended),{}).bodies.empty(),"extended empty group and descriptor catalog");
    auto broken=extended; broken[2]=0x34;
    rejects([&] { RepresentationReader::read(binaryFixture(broken),{}); });
    broken=extended; broken[broken.size()-1]=0x40;
    rejects([&] { RepresentationReader::read(binaryFixture(broken),{}); });
    QByteArray sparse=emptyGroup.left(41)+QByteArray::fromHex("ff020000000101")+emptyGroup+QByteArray::fromHex("0101");
    require(RepresentationReader::read(binaryFixture(sparse),{}).bodies.empty(),"sparse cache definition and reference");
    QByteArray material=QByteArray(1,char(0x62))+QByteArray(35,'\0')+QByteArray::fromHex("ff030101000001")+QByteArray(15*4,'\0');
    material+=QByteArray::fromHex("000001010500010100")+QByteArray(19*4,'\0')+QByteArray::fromHex("0100ff00000000");
    require(RepresentationReader::read(binaryFixture(material),{}).bodies.empty(),"image-free graph material");

    QByteArray packed=QByteArray::fromHex("efcdab89ff785634120000c03f");
    PackedReader bits(packed);
    require(bits.bits(5)==(0x89abcdefU&31),"packed low bits");
    require(bits.count(0xffffffffU)==0x12345678U,"interleaved extended count");
    require(bits.scalar()==1.5f,"interleaved float");
    require(bits.bits(27)==(0x89abcdefU>>5) && bits.available==0,"reservoir survives raw fields");
    rejects([&] { bits.bits(1); });
    QTemporaryDir temp; require(temp.isValid(),"fixture directory");
    const QString zip=temp.filePath("fixture.3dxml");
    const QByteArray marker="checksum-fixture-payload";
    writeZip(zip,{{"Manifest.xml",marker}});
    {
        ArchiveReader reader(zip,{});
        require(reader.read("Manifest.xml")==marker,"ZIP member read");
        rejects([&] { reader.read("absent.xml"); });
    }
    const QString unicode=temp.filePath(QString::fromUtf8("\xe6\xa8\xa1\xe5\x9e\x8b.3dxml"));
    require(QFile::copy(zip,unicode),"Unicode archive fixture");
    { ArchiveReader reader(unicode,{}); require(reader.read("Manifest.xml")==marker,"Unicode archive path"); }
    {
        QFile corrupt(zip); require(corrupt.open(QIODevice::ReadWrite),"corrupt CRC fixture");
        const auto bytes=corrupt.readAll(); const auto at=bytes.indexOf(marker);
        require(at>=0 && corrupt.seek(at),"find stored payload"); corrupt.write("X",1);
    }
    rejects([&] { ArchiveReader reader(zip,{}); reader.read("Manifest.xml"); });
    writeZip(zip,{{"Manifest.xml",marker},{"Manifest.xml",marker}});
    rejects([&] { ArchiveReader reader(zip,{}); });
    const QByteArray cycle=R"(<ProductStructure root='1'><Reference3D id='1'/><Instance3D id='2'><IsAggregatedBy>1</IsAggregatedBy><IsInstanceOf>1</IsInstanceOf><RelativeMatrix>1 0 0 0 1 0 0 0 1 0 0 0</RelativeMatrix></Instance3D></ProductStructure>)";
    writeZip(zip,{{"Manifest.xml","<m><Root>main.xml</Root></m>"},{"main.xml",cycle}});
    bool rejectedCycle=false;
    try { Importer::read(zip); } catch(const Error& e) { rejectedCycle=QString::fromUtf8(e.what()).contains("Cyclic"); }
    require(rejectedCycle,"reference cycle rejected");
    auto root=std::make_unique<aiNode>();
    root->mNumChildren=2; root->mChildren=new aiNode*[2]{};
    for (int i=0;i<2;++i) {
        root->mChildren[i]=new aiNode(); root->mChildren[i]->mParent=root.get();
        root->mChildren[i]->mNumMeshes=1; root->mChildren[i]->mMeshes=new unsigned[1]{0};
    }
    require(countSceneMeshOccurrences(root.get())==2,"shared mesh progress counts occurrences");
    rejects([] { ArchiveReader::memberPath("../escape"); });
    rejects([] { ArchiveReader::memberPath("C:/external"); });
    require(ArchiveReader::memberPath("urn:3DXML:part.3DRep")=="part.3DRep","URN path");
    rejects([] { ProductStructureReader::rootMember("<Manifest>"); });
    rejects([] { ProductStructureReader::rootMember("<!DOCTYPE m [<!ENTITY e 'x'>]><m><Root>&e;</Root></m>"); });
    rejects([] { ProductStructureReader::read("<ProductStructure root='1'><Reference3D id='1'/><Reference3D id='1'/></ProductStructure>",{}); });
    rejects([] { RepresentationReader::read(QByteArray("unknown"),{}); });
    const QByteArray xml=R"(<Model><ProductStructure root='1'>
      <Reference3D id='1' name='root'/><Reference3D id='2' name='assembly'/><Reference3D id='3' name='part'/>
      <Instance3D id='10' name='outer'><IsAggregatedBy>1</IsAggregatedBy><IsInstanceOf>2</IsInstanceOf><RelativeMatrix>0 1 0 -1 0 0 0 0 1 10 20 30</RelativeMatrix></Instance3D>
      <Instance3D id='11' name='inner'><IsAggregatedBy>2</IsAggregatedBy><IsInstanceOf>3</IsInstanceOf><RelativeMatrix>1 0 0 0 0 1 0 -1 0 2 3 4</RelativeMatrix></Instance3D>
      </ProductStructure><DefaultViewProperty><OccurrenceId><id>urn:x#10</id><id>urn:x#11</id></OccurrenceId><GeneralAttributes visible='false'/></DefaultViewProperty></Model>)";
    auto doc=ProductStructureReader::read(xml,{});
    require(doc.references["1"].instances==QStringList{"10"},"XML order/reference links");
    aiVector3D p(1,2,3); auto actual=doc.instances["10"].local*doc.instances["11"].local*p;
    // Inner point=(3,0,6); outer=(-y+10,x+20,z+30).
    require((actual-aiVector3D(10,23,36)).Length()<1e-6,"nested noncommuting matrices");
    require(doc.hiddenPaths.contains("10/11"),"visibility occurrence path");
    Options cancel; cancel.cancelled=[] { return true; };
    rejects([&] { ProductStructureReader::read(xml,cancel); });
    const QByteArray tessellated=R"(<XMLRepresentation xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance'>
      <Root xsi:type='BagRepType'><Rep xsi:type='PolygonalRepType'>
      <SurfaceAttributes><Color red='1' green='0' blue='0'/></SurfaceAttributes>
      <Faces><Face triangles='0 1 2'/><Face strips='0 1 2 3'><SurfaceAttributes><Color red='0' green='1' blue='0'/></SurfaceAttributes></Face><Face fans='0 1 2 3'/></Faces>
      <PolygonalLOD><Faces><Face triangles='99 99 99'/></Faces></PolygonalLOD>
      <VertexBuffer><Positions>0 0 0,1 0 0,0 1 0,1 1 0</Positions><Normals>0 0 1,0 0 1,0 0 1,0 0 1</Normals></VertexBuffer>
      </Rep></Root></XMLRepresentation>)";

    const QByteArray tessellatedProduct=R"(<ProductStructure root='1'><Reference3D id='1' name='test'/>
      <ReferenceRep id='2' format='TESSELLATED' associatedFile='part.3DRep'/>
      <InstanceRep id='3'><IsAggregatedBy>1</IsAggregatedBy><IsInstanceOf>2</IsInstanceOf></InstanceRep></ProductStructure>)";
    writeZip(zip,{{"Manifest.xml","<m><Root>main.xml</Root></m>"},{"main.xml",tessellatedProduct},{"part.3DRep",tessellated}});
    auto saveFixture=[&](const QString& name,const QByteArray& bytes) {
        QFile file(temp.filePath(name)); require(file.open(QIODevice::WriteOnly),"write loose fixture"); require(file.write(bytes)==bytes.size(),"complete loose fixture");
    };
    const QByteArray looseXml=QByteArray::fromHex("efbbbf")+"<Model_3dxml>"+tessellatedProduct+"</Model_3dxml>";
    saveFixture("loose.3dxml",looseXml); saveFixture("part.3DRep",tessellated);
    auto loose=Importer::read(temp.filePath("loose.3dxml"));
    require(loose.scene->mNumMeshes==1&&countSceneMeshOccurrences(loose.scene->mRootNode)==1,"loose XML model: one body, two face colours, one vertex-coloured mesh");
    require(loose.scene->mMeshes[0]->HasVertexColors(0),"face colours travel as vertex colours");
    { ArchiveReader directory(temp.filePath("loose.3dxml"),{});
        require(directory.rootDocument()=="loose.3dxml"&&directory.read("part.3DRep")==tessellated,"loose root/member resolution");
        rejects([&] { directory.read("absent.3DRep"); });
        rejects([&] { directory.read("../outside.3DRep"); });
    }
    saveFixture("material.3dxml","<Model_3dxml><CATMaterialRef/></Model_3dxml>");
    bool companionError=false;
    try { Importer::read(temp.filePath("material.3dxml")); }
    catch (const Error& e) { companionError=QString::fromUtf8(e.what()).contains("companion resources"); }
    require(companionError,"companion-only document diagnostic");
    saveFixture("dtd.3dxml","<!DOCTYPE Model_3dxml><Model_3dxml/>");
    rejects([&] { Importer::read(temp.filePath("dtd.3dxml")); });
    saveFixture("invalid.3dxml","garbage"); rejects([&] { Importer::read(temp.filePath("invalid.3dxml")); });
    Options cancelledLoose; cancelledLoose.cancelled=[] { return true; };
    rejects([&] { Importer::read(temp.filePath("loose.3dxml"),cancelledLoose); });
#ifndef Q_OS_WIN
    QTemporaryDir outside;
    QFile outsideFile(outside.filePath("secret")); require(outsideFile.open(QIODevice::WriteOnly),"outside link fixture"); outsideFile.write("secret"); outsideFile.close();
    require(QFile::link(outsideFile.fileName(),temp.filePath("outside-link")),"symlink fixture");
    { ArchiveReader directory(temp.filePath("loose.3dxml"),{}); rejects([&] { directory.read("outside-link"); }); }
#endif

    auto importedTessellation=Importer::read(zip);
    require(importedTessellation.scene->mNumMeshes==1&&countSceneMeshOccurrences(importedTessellation.scene->mRootNode)==1,"XML meshes attached to product scene");


    QByteArray coloredProduct=R"(<Model><ProductStructure root='1'>
      <Reference3D id='1' name='root'/><Reference3D id='2' name='assembly'/><Reference3D id='3' name='part'/>
      <ReferenceRep id='4' format='TESSELLATED' associatedFile='part.3DRep'/>
      <InstanceRep id='5'><IsAggregatedBy>3</IsAggregatedBy><IsInstanceOf>4</IsInstanceOf></InstanceRep>
      <Instance3D id='10'><IsAggregatedBy>1</IsAggregatedBy><IsInstanceOf>2</IsInstanceOf><RelativeMatrix>1 0 0 0 1 0 0 0 1 0 0 0</RelativeMatrix></Instance3D>
      <Instance3D id='11'><IsAggregatedBy>2</IsAggregatedBy><IsInstanceOf>3</IsInstanceOf><RelativeMatrix>1 0 0 0 1 0 0 0 1 0 0 0</RelativeMatrix></Instance3D>
      <Instance3D id='12'><IsAggregatedBy>2</IsAggregatedBy><IsInstanceOf>3</IsInstanceOf><RelativeMatrix>1 0 0 0 1 0 0 0 1 0 0 0</RelativeMatrix></Instance3D>
      <Instance3D id='13'><IsAggregatedBy>1</IsAggregatedBy><IsInstanceOf>3</IsInstanceOf><RelativeMatrix>1 0 0 0 1 0 0 0 1 0 0 0</RelativeMatrix></Instance3D>
      <Instance3D id='14'><IsAggregatedBy>1</IsAggregatedBy><IsInstanceOf>3</IsInstanceOf><RelativeMatrix>1 0 0 0 1 0 0 0 1 0 0 0</RelativeMatrix></Instance3D>
      </ProductStructure><DefaultView>
      <DefaultViewProperty><OccurenceId><id>urn:test#10</id></OccurenceId><GraphicProperties><SurfaceAttributes><Color red='0' green='0' blue='1' alpha='0.25'/></SurfaceAttributes></GraphicProperties></DefaultViewProperty>
      <DefaultViewProperty><OccurenceId><id>urn:test#10</id><id>urn:test#12</id></OccurenceId><GraphicProperties><SurfaceAttributes><Color red='1' green='1' blue='0'/></SurfaceAttributes></GraphicProperties></DefaultViewProperty>
      <DefaultViewProperty><OccurenceId><id>urn:test#14</id></OccurenceId><GraphicProperties><SurfaceAttributes><Color red='0' green='0' blue='1' alpha='0.25'/></SurfaceAttributes></GraphicProperties></DefaultViewProperty>
      </DefaultView></Model>)";
    auto coloredDoc=ProductStructureReader::read(coloredProduct,{});
    require(coloredDoc.occurrenceColors.size()==3&&coloredDoc.occurrenceColors.value("10/12").a==1,"occurrence color paths/default alpha");
    writeZip(zip,{{"Manifest.xml","<m><Root>main.xml</Root></m>"},{"main.xml",coloredProduct},{"part.3DRep",tessellated}});
    auto colored=Importer::read(zip); const auto& coloredScene=*colored.scene;
    // One vertex-coloured base mesh, plus a blue and a yellow variant that replace its face colours; 4 occurrences.
    require(coloredScene.mNumMeshes==3&&countSceneMeshOccurrences(coloredScene.mRootNode)==4,"material variants preserve instances");
    QHash<QString,const aiNode*> colorNodes;
    for(auto it=colored.occurrencePaths.cbegin();it!=colored.occurrencePaths.cend();++it) colorNodes.insert(it.value(),it.key());
    auto colorAt=[&](const QString& path,unsigned slot) {
        const auto n=colorNodes.value(path); require(n&&slot<n->mNumMeshes,"color occurrence mesh");
        const auto m=coloredScene.mMeshes[n->mMeshes[slot]]; aiColor4D rgba;
        require(coloredScene.mMaterials[m->mMaterialIndex]->Get(AI_MATKEY_BASE_COLOR,rgba)==AI_SUCCESS,"occurrence base color"); return rgba;
    };
    for(unsigned slot=0;slot<1;++slot) {
        const auto blue=colorAt("10/11",slot),yellow=colorAt("10/12",slot);
        require(blue==aiColor4D(0,0,1,.25f)&&yellow==aiColor4D(1,1,0,1),"inherited and child occurrence override");
        require(colorNodes["10/11"]->mMeshes[slot]==colorNodes["14"]->mMeshes[slot],"same override reuses variant");
        const auto a=coloredScene.mMeshes[colorNodes["10/11"]->mMeshes[slot]],b=coloredScene.mMeshes[colorNodes["10/12"]->mMeshes[slot]];
        require(a->mVertices!=b->mVertices&&a->mNumVertices==b->mNumVertices&&a->mNumFaces==b->mNumFaces,"variant owns independent geometry buffers");
        require(!a->HasVertexColors(0)&&!b->HasVertexColors(0),"occurrence colour replaces the face colours");
        for(unsigned i=0;i<a->mNumVertices;++i) require(a->mVertices[i]==b->mVertices[i]&&a->mNormals[i]==b->mNormals[i],"variant geometry unchanged");
    }
    {
        const auto& plain=*coloredScene.mMeshes[colorNodes["13"]->mMeshes[0]];
        require(colorAt("13",0)==aiColor4D(1,1,1,1)&&plain.HasVertexColors(0),"uncolored instance keeps its face colors as vertex colors");
        bool red=false,green=false;
        for(unsigned i=0;i<plain.mNumVertices;++i) { const auto& c=plain.mColors[0][i]; red=red||c==aiColor4D(1,0,0,1); green=green||c==aiColor4D(0,1,0,1); }
        require(red&&green,"both face colors present on the vertices");
    }
    auto invalidColor=coloredProduct; invalidColor.replace("blue='1'","blue='2'");
    rejects([&] { ProductStructureReader::read(invalidColor,{}); });
    invalidColor=coloredProduct; invalidColor.replace("urn:test#14","urn:test#999");
    writeZip(zip,{{"Manifest.xml","<m><Root>main.xml</Root></m>"},{"main.xml",invalidColor},{"part.3DRep",tessellated}});
    rejects([&] { Importer::read(zip); });

    auto geometry=TessellatedReader::read(tessellated,{});
    require(geometry.bodies.size()==1&&geometry.occurrences.size()==1,"XML body occurrence");
    const auto& body=geometry.bodies.front();
    require(body.positions.size()==4&&body.normals.size()==4&&body.faces.size()==3,"XML buffers and finest LOD");
    require(body.faces[0].rgb==0xff0000&&body.faces[1].rgb==0x00ff00,"XML inherited and face colors");
    require(body.faces[1].triangles==std::vector<std::array<unsigned,3>>{{0,1,2},{2,1,3}},"XML strip winding");
    require(body.faces[2].triangles==std::vector<std::array<unsigned,3>>{{0,1,2},{0,2,3}},"XML fan winding");
    auto invalid=tessellated; invalid.replace("triangles='0 1 2'","triangles='0 1 8'");
    rejects([&] { TessellatedReader::read(invalid,{}); });
    invalid=tessellated; invalid.replace("0 0 1,0 0 1,0 0 1,0 0 1","0 0 1");
    rejects([&] { TessellatedReader::read(invalid,{}); });
    rejects([&] { TessellatedReader::read(tessellated,cancel); });
    rejects([&] { TessellatedReader::read("<!DOCTYPE x><XMLRepresentation/>",{}); });

}

void appendFloat(QByteArray& out,float value) { quint32 bits; std::memcpy(&bits,&value,4); uchar bytes[4]; qToLittleEndian(bits,bytes); out.append(reinterpret_cast<const char*>(bytes),4); }

// Record variants written by CATIA/3DEXPERIENCE exports (grammars derived from a production sample; see
// research/3dxml/3DXML_published_documentation_findings.md). Each is a complete skeleton, byte for byte.
void variants()
{
    const QByteArray hdr(35,'\0');
    // A small group-flags byte followed by 00 01 00 00 carries the 0x100 flag; a plain small flags byte must still parse.
    require(RepresentationReader::read(binaryFixture(QByteArray(1,char(0x62))+hdr+QByteArray::fromHex("0100010000ff00000000")),{}).bodies.empty(),"small flags + 00 01 00 00");
    require(RepresentationReader::read(binaryFixture(QByteArray(1,char(0x62))+hdr+QByteArray::fromHex("01ff00000000")),{}).bodies.empty(),"plain small flags");
    // Group tag 9a: like a3, but the trailer is a compact count (0) and the span ends one byte past it.
    const QByteArray group9a=QByteArray::fromHex("9aff36000000")+hdr+QByteArray::fromHex("ff03010000ff00000000ff00000000");
    require(RepresentationReader::read(binaryFixture(group9a),{}).bodies.empty(),"group tag 9a");
    auto wrongSpan=group9a; wrongSpan[2]=0x35;
    rejects([&] { RepresentationReader::read(binaryFixture(wrongSpan),{}); });
    // Leaf tag a1: skipped by its span, followed by one compact value that is not a surface offset.
    const QByteArray leafA1=QByteArray::fromHex("a1ff0f000000")+QByteArray::fromHex("00112233445566778899")+QByteArray::fromHex("01");
    require(RepresentationReader::read(binaryFixture(leafA1),{}).bodies.empty(),"leaf tag a1 skipped by span");
    auto longSpan=leafA1; longSpan[2]=0x40;
    rejects([&] { RepresentationReader::read(binaryFixture(longSpan),{}); });
    // Curve record 78: no span; polyline, two end points and a style.
    auto curve=[&](unsigned points,const QByteArray& style) {
        QByteArray record=QByteArray::fromHex("78ff85feffff")+hdr+QByteArray::fromHex("ffff0200");
        appendFloat(record,0.05f); appendFloat(record,0.785398f);
        record+=QByteArray::fromHex("a5d46853")+QByteArray::fromHex("01")+QByteArray::fromHex("79000000")+QByteArray::fromHex("ffff");
        record+=QByteArray(1,char(points))+QByteArray::fromHex("01");
        for(unsigned i=0;i<points;++i) { appendFloat(record,41.f); appendFloat(record,float(i)); appendFloat(record,2.5f); }
        record+=QByteArray::fromHex("001400")+QByteArray::fromHex("ffff00ff00ff02000000");
        for(int end=0;end<2;++end) { record+=QByteArray::fromHex("78000000")+QByteArray::fromHex("0000")+QByteArray::fromHex("0501"); appendFloat(record,41.f); appendFloat(record,0.f); appendFloat(record,2.5f); }
        return record+style;
    };
    const QByteArray curveStyle=QByteArray::fromHex("000404f9ff000000");
    require(RepresentationReader::read(binaryFixture(curve(21,curveStyle)),{}).bodies.empty(),"curve record 78");
    rejects([&] { RepresentationReader::read(binaryFixture(curve(21,QByteArray::fromHex("300404ffff000000"))),{}); });
    // Leaf 64: ids and a colour style only.
    const QByteArray leaf64=QByteArray::fromHex("64ffb5ffffff")+QByteArray(35,'\0')+QByteArray::fromHex("ffff020001")+QByteArray::fromHex("5469")+QByteArray(5,'\0')+QByteArray::fromHex("0101")+QByteArray(13,'\0')+QByteArray::fromHex("0504ffff9999cc");
    require(RepresentationReader::read(binaryFixture(leaf64),{}).bodies.empty(),"leaf 64");
    auto brokenLeaf=leaf64; brokenLeaf[brokenLeaf.size()-5]=0x00;
    rejects([&] { RepresentationReader::read(binaryFixture(brokenLeaf),{}); });
}

// Scene building: merging heavily split parts, vertex colours, provenance metadata and the import tally.
void sceneVariants()
{
    QTemporaryDir temp; require(temp.isValid(),"fixture directory");
    const QString zip=temp.filePath("variants.3dxml");
    auto representation=[](unsigned bodies) {
        QByteArray xml="<XMLRepresentation xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance'><Root xsi:type='BagRepType'>";
        for(unsigned i=0;i<bodies;++i) {
            const char* color=(i%2) ? "red='1' green='0' blue='0'" : "red='0' green='1' blue='0'";
            xml+=QByteArray("<Rep xsi:type='PolygonalRepType'><SurfaceAttributes><Color ")+color+"/></SurfaceAttributes><Faces><Face triangles='0 1 2'/></Faces>"
                "<VertexBuffer><Positions>0 0 0,1 0 0,0 1 0</Positions><Normals>0 0 1,0 0 1,0 0 1</Normals></VertexBuffer></Rep>";
        }
        return xml+"</Root></XMLRepresentation>";
    };
    const QByteArray product=R"(<ProductStructure root='1'><Reference3D id='1' name='test'/>
      <ReferenceRep id='2' name='partrep' format='TESSELLATED' associatedFile='part.3DRep'/>
      <InstanceRep id='3'><IsAggregatedBy>1</IsAggregatedBy><IsInstanceOf>2</IsInstanceOf></InstanceRep></ProductStructure>)";
    auto warned=[](const ImportResult& result,const char* text) { for(const auto& w:result.warnings) if(w.contains(QLatin1String(text))) return true; return false; };

    // Several leaves (any number) form one part: one mesh per opacity group, vertices keep their leaf number, the node names its representation.
    writeZip(zip,{{"Manifest.xml","<m><Root>main.xml</Root></m>"},{"main.xml",product},{"part.3DRep",representation(51)}});
    auto merged=Importer::read(zip); const auto& mergedScene=*merged.scene;
    require(mergedScene.mNumMeshes==1,"51 leaves merge into one mesh");
    const auto& mesh=*mergedScene.mMeshes[0];
    require(QString::fromUtf8(mesh.mName.C_Str())==QStringLiteral("test [part.3DRep__merged]"),"merged mesh name carries the part name and the representation member");
    require(mesh.HasVertexColors(0)&&mesh.mNumVertices==51*3&&mesh.mNumFaces==51,"merged geometry and colours");
    const auto leafIds=merged.meshLeafIds.value(&mesh);
    require(leafIds&&leafIds->size()==mesh.mNumVertices,"one leaf number per vertex");
    require(std::set<unsigned>(leafIds->begin(),leafIds->end()).size()==51,"each leaf keeps its own number");
    require(warned(merged,"merged into")&&warned(merged,"3DXML tally"),"merge and tally are reported");
    aiString member; require(mergedScene.mRootNode->mMetaData&&mergedScene.mRootNode->mMetaData->Get("3DXMLRepresentation",member)&&member==aiString("part.3DRep"),"node names its representation");

    // Two leaves already merge: there is no size threshold. A single leaf is left as it is, with its material colour.
    writeZip(zip,{{"Manifest.xml","<m><Root>main.xml</Root></m>"},{"main.xml",product},{"part.3DRep",representation(2)}});
    auto two=Importer::read(zip);
    require(two.scene->mNumMeshes==1&&two.scene->mMeshes[0]->HasVertexColors(0),"2 leaves of different colours merge into one mesh with vertex colours");
    writeZip(zip,{{"Manifest.xml","<m><Root>main.xml</Root></m>"},{"main.xml",product},{"part.3DRep",representation(1)}});
    auto kept=Importer::read(zip); const auto& keptScene=*kept.scene;
    require(keptScene.mNumMeshes==1&&!keptScene.mMeshes[0]->HasVertexColors(0)&&kept.meshLeafIds.isEmpty(),"a single leaf keeps a material colour and is not merged");
    require(!warned(kept,"merged into")&&warned(kept,"3DXML tally"),"no merge report for a single leaf");

    // An unreadable representation is skipped and listed; the rest of the assembly loads. Strict mode keeps all-or-nothing.
    const QByteArray twoReps=R"(<ProductStructure root='1'><Reference3D id='1' name='test'/>
      <ReferenceRep id='2' name='goodrep' format='TESSELLATED' associatedFile='part.3DRep'/>
      <ReferenceRep id='6' name='badrep' format='TESSELLATED' associatedFile='bad.3DRep'/>
      <InstanceRep id='3'><IsAggregatedBy>1</IsAggregatedBy><IsInstanceOf>2</IsInstanceOf></InstanceRep>
      <InstanceRep id='5'><IsAggregatedBy>1</IsAggregatedBy><IsInstanceOf>6</IsInstanceOf></InstanceRep></ProductStructure>)";
    writeZip(zip,{{"Manifest.xml","<m><Root>main.xml</Root></m>"},{"main.xml",twoReps},{"part.3DRep",representation(2)},{"bad.3DRep","this is not xml"}});
    auto partial=Importer::read(zip);
    require(partial.scene->mNumMeshes==1,"the readable representation still loads");
    require(warned(partial,"Skipped unreadable representation bad.3DRep")&&warned(partial,"1 of 2 representations could not be read"),"the skipped member is listed by name");
    Options strict; strict.strict=true;
    rejects([&] { Importer::read(zip,strict); });
    writeZip(zip,{{"Manifest.xml","<m><Root>main.xml</Root></m>"},{"main.xml",twoReps},{"part.3DRep","also not xml"},{"bad.3DRep","this is not xml"}});
    bool noGeometry=false;
    try { Importer::read(zip); } catch(const Error& e) { noGeometry=QString::fromUtf8(e.what()).contains(QLatin1String("no readable geometry; first failure")); }
    require(noGeometry,"all representations unreadable still fails, naming the first failure");
}

void sample(const QString& archive, const QString& glbPath, const QString& occurrencesPath, const QString& output)
{
    auto result=Importer::read(archive);
    Options cancelled; int checks=0; cancelled.cancelled=[&] { return ++checks>20; };
    rejects([&] { Importer::read(archive,cancelled); });
    const auto& scene=*result.scene;
    QJsonObject report;
    report["mesh_resources"]=int(scene.mNumMeshes); report["materials"]=int(scene.mNumMaterials);
    quint64 triangles=0,visibleTriangles=0; int occurrenceCount=0,visibleMeshes=0,visibleBodies=0,nodes=0,xmlNodes=0,hiddenXml=0;
    aiVector3D low(1e30f,1e30f,1e30f),high(-1e30f,-1e30f,-1e30f);
    auto fixture=QJsonDocument::fromJson(readFile(occurrencesPath)).object();
    std::map<QString,QJsonObject> placements;
    for (const auto& v:fixture["occurrences"].toArray()) {
        auto o=v.toObject(); QStringList path; for(const auto& id:o["path"].toArray()) path.append(id.toString()); placements[path.join('/')]=o;
    }
    std::function<void(const aiNode*,const aiMatrix4x4&)> walk=[&](const aiNode* n,const aiMatrix4x4& parent) {
        ++nodes; aiMatrix4x4 world=parent*n->mTransformation;
        auto it=result.occurrencePaths.constFind(n);
        bool visible=!result.hiddenNodes.contains(n);
        if(it!=result.occurrencePaths.cend()) {
            ++xmlNodes;
            if(!it.value().isEmpty()) {
                const auto o=placements.at(it.value());
                require(o["name"].toString()==QString::fromUtf8(n->mName.C_Str()),"occurrence name");
                require(o["visible"].toBool()==visible,"default visibility"); hiddenXml+=!visible;
                require(result.occurrencePaths.value(n->mParent)==it.value().section('/',0,-2),"XML parent link");
                auto local=o["local_matrix_rows"].toArray();
                for(int r=0;r<4;++r) for(int c=0;c<4;++c) require(std::abs(n->mTransformation[r][c]-local[r].toArray()[c].toDouble())<.00002,"local placement");
                auto matrix=o["world_matrix_rows"].toArray();
                for(int r=0;r<4;++r) for(int c=0;c<4;++c) require(std::abs(world[r][c]-matrix[r].toArray()[c].toDouble())<.0002,"world placement");
            }
        }
        if(visible && n->mNumMeshes) ++visibleBodies;
        for(unsigned i=0;i<n->mNumMeshes;++i) {
            ++occurrenceCount; const auto& mesh=*scene.mMeshes[n->mMeshes[i]];
            if(visible) {
                ++visibleMeshes; visibleTriangles+=mesh.mNumFaces;
                for(unsigned j=0;j<mesh.mNumVertices;++j) { auto p=world*mesh.mVertices[j];
                    for(int k=0;k<3;++k) { low[k]=std::min(low[k],p[k]); high[k]=std::max(high[k],p[k]); }
                }
            }
        }
        for(unsigned i=0;i<n->mNumChildren;++i) walk(n->mChildren[i],world);
    };
    walk(scene.mRootNode,aiMatrix4x4());
    const auto bytes=readFile(glbPath);
    require(bytes.left(4)=="glTF","GLB reference signature");
    auto u32=[&](qsizetype at) { return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData()+at)); };
    quint32 jsonLength=u32(12); auto glb=QJsonDocument::fromJson(bytes.mid(20,jsonLength)).object();
    qsizetype binStart=20+jsonLength+8;
    const auto accessors=glb["accessors"].toArray(),views=glb["bufferViews"].toArray(),mats=glb["materials"].toArray();
    std::vector<qsizetype> offsets;
    for(const auto& value:accessors) {
        auto a=value.toObject(); auto view=views[a["bufferView"].toInt()].toObject();
        offsets.push_back(binStart+view["byteOffset"].toInt());
    }
    auto vector=[&](int accessor,unsigned index) {
        auto at=offsets.at(accessor)+12*index; aiVector3D v;
        for(int k=0;k<3;++k) { quint32 bits=u32(at+4*k); float f; std::memcpy(&f,&bits,4); v[k]=f; } return v;
    };
    std::map<QString,QJsonObject> primitives;
    for(const auto& m:glb["meshes"].toArray()) {
        auto mesh=m.toObject(); auto member=mesh["extras"].toObject()["sourceMember"].toString();
        for(const auto& p:mesh["primitives"].toArray()) {
            auto prim=p.toObject(); auto offset=prim["extras"].toObject()["sourceLeafOffset"].toInt();
            QString color=mats[prim["material"].toInt()].toObject()["name"].toString();
            primitives[member+"__body_"+QString::number(offset)+"/"+color]=prim;
        }
    }
    int compared=0;
    for(unsigned mi=0;mi<scene.mNumMeshes;++mi) {
        const auto& mesh=*scene.mMeshes[mi]; triangles+=mesh.mNumFaces;
        aiString name; scene.mMaterials[mesh.mMaterialIndex]->Get(AI_MATKEY_NAME,name);
        // The reference has one primitive per body and face colour. A mesh with vertex colours holds several colours: split its
        // triangles back by colour (every triangle's corners share one colour) and compare each group with its reference primitive.
        std::map<QString,std::vector<unsigned>> byColor;
        if(mesh.HasVertexColors(0)) {
            for(unsigned f=0;f<mesh.mNumFaces;++f) {
                const auto& c=mesh.mColors[0][mesh.mFaces[f].mIndices[0]];
                const unsigned rgb=(unsigned(std::lround(c.r*255))<<16)|(unsigned(std::lround(c.g*255))<<8)|unsigned(std::lround(c.b*255));
                byColor[QStringLiteral("face_rgb_%1").arg(rgb,6,16,QLatin1Char('0'))].push_back(f);
            }
        } else {
            std::vector<unsigned>& all=byColor[QString::fromUtf8(name.C_Str())];
            for(unsigned f=0;f<mesh.mNumFaces;++f) all.push_back(f);
        }
        for(const auto& group:byColor) {
            auto found=primitives.find(sourceMeshName(mesh)+'/'+group.first);
            if(found==primitives.end()) continue; // GLB intentionally omitted hidden-only resources.
            ++compared; auto attrs=found->second["attributes"].toObject();
            const auto pbr=mats[found->second["material"].toInt()].toObject()["pbrMetallicRoughness"].toObject();
            const auto rgba=pbr["baseColorFactor"].toArray(); aiColor4D color;
            if(mesh.HasVertexColors(0)) color=mesh.mColors[0][mesh.mFaces[group.second.front()].mIndices[0]];
            else require(scene.mMaterials[mesh.mMaterialIndex]->Get(AI_MATKEY_BASE_COLOR,color)==AI_SUCCESS,"base color property");
            for(int k=0;k<3;++k) require(std::abs(color[k]-rgba[k].toDouble())<1e-7,"material color parity");
            int position=attrs["POSITION"].toInt(),normal=attrs["NORMAL"].toInt();
            require(accessors[position].toObject()["count"].toInt()==int(group.second.size()*3),"primitive triangle count");
            for(size_t n=0;n<group.second.size();++n) for(unsigned c=0;c<3;++c) {
                auto i=mesh.mFaces[group.second[n]].mIndices[c];
                require((mesh.mVertices[i]-vector(position,unsigned(n*3+c))).SquareLength()==0,"source position parity");
                require((mesh.mNormals[i]-vector(normal,unsigned(n*3+c))).Length()<1e-6,"source normal parity");
            }
        }
    }
    require(compared==int(primitives.size()),"all reference primitives checked");
    require(xmlNodes==252 && hiddenXml==17 && visibleBodies==276 && visibleMeshes<=538 && visibleTriangles==5608912,"sample scene counts");
    require(triangles==4152382,"complete source triangle count");
    report["nodes"]=nodes; report["xml_nodes"]=xmlNodes; report["hidden_xml_occurrences"]=hiddenXml;
    report["mesh_occurrences"]=occurrenceCount; report["visible_meshes"]=visibleMeshes;
    report["visible_bodies"]=visibleBodies;
    report["source_sha256"]=QString::fromLatin1(QCryptographicHash::hash(readFile(archive),QCryptographicHash::Sha256).toHex());
    report["reference_glb_sha256"]=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
    report["resource_triangles"]=double(triangles); report["visible_triangles"]=double(visibleTriangles);
    report["compared_glb_primitives"]=compared; report["passed"]=true;
    report["bounds_native_min"]=QJsonArray{low.x,low.y,low.z}; report["bounds_native_max"]=QJsonArray{high.x,high.y,high.z};
    QFile out(output); require(out.open(QIODevice::WriteOnly),"cannot write audit"); out.write(QJsonDocument(report).toJson());
    std::cout<<QJsonDocument(report).toJson().constData();
}

void audit(const QString& path,const QString& output) {
    auto result=Importer::read(path); const auto& scene=*result.scene;
    ArchiveReader sourceArchive(path,{});
    const auto product=ProductStructureReader::read(sourceArchive.read(sourceArchive.rootDocument().isEmpty()?ProductStructureReader::rootMember(sourceArchive.read("Manifest.xml")):sourceArchive.rootDocument()),{});
    QJsonArray appearances;
    quint64 triangles=0,vertices=0,visibleTriangles=0; unsigned nodes=0,visibleMeshes=0;
    for(unsigned i=0;i<scene.mNumMeshes;++i) {
        const auto& mesh=*scene.mMeshes[i]; triangles+=mesh.mNumFaces; vertices+=mesh.mNumVertices;
        require(mesh.HasNormals(),"audit normals");
        for(unsigned j=0;j<mesh.mNumVertices;++j) for(int k=0;k<3;++k)
            require(std::isfinite(mesh.mVertices[j][k])&&std::isfinite(mesh.mNormals[j][k]),"audit finite geometry");
        for(unsigned j=0;j<mesh.mNumFaces;++j) { require(mesh.mFaces[j].mNumIndices==3,"audit triangles");
            for(unsigned k=0;k<3;++k) require(mesh.mFaces[j].mIndices[k]<mesh.mNumVertices,"audit indices"); }
    }
    aiVector3D low(1e30f,1e30f,1e30f),high(-1e30f,-1e30f,-1e30f);
    std::function<void(const aiNode*,const aiMatrix4x4&)> walk=[&](const aiNode* n,const aiMatrix4x4& parent) {
        ++nodes; auto world=parent*n->mTransformation;
        const QString occurrence=result.occurrencePaths.value(n,result.occurrencePaths.value(n->mParent));
        QString ancestor=occurrence;
        while(!ancestor.isEmpty()&&!product.occurrenceColors.contains(ancestor)) ancestor=ancestor.contains('/')?ancestor.section('/',0,-2):QString();
        if(n->mNumMeshes) {
            QJsonArray colors,names,triangleCounts;
            for(unsigned i=0;i<n->mNumMeshes;++i) {
                const auto mesh=scene.mMeshes[n->mMeshes[i]]; aiColor4D base,diffuse; ai_real opacity=0;
                const auto mat=scene.mMaterials[mesh->mMaterialIndex];
                require(mat->Get(AI_MATKEY_BASE_COLOR,base)==AI_SUCCESS&&mat->Get(AI_MATKEY_COLOR_DIFFUSE,diffuse)==AI_SUCCESS,"audit color properties");
                require(mat->Get(AI_MATKEY_OPACITY,opacity)==AI_SUCCESS&&base==diffuse&&base.a==opacity,"audit color/opacity bindings");
                colors.append(QJsonArray{base.r,base.g,base.b,base.a});
                names.append(sourceMeshName(*mesh)); triangleCounts.append(int(mesh->mNumFaces));
            }
            appearances.append(QJsonObject{{"path",occurrence},{"colors",colors},{"meshes",names},{"triangles",triangleCounts},{"overridden",product.occurrenceColors.contains(ancestor)}});
        }
        if(!result.hiddenNodes.contains(n)) for(unsigned i=0;i<n->mNumMeshes;++i) {
            ++visibleMeshes; const auto& mesh=*scene.mMeshes[n->mMeshes[i]]; visibleTriangles+=mesh.mNumFaces;
            for(unsigned j=0;j<mesh.mNumVertices;++j) { auto p=world*mesh.mVertices[j]; for(int k=0;k<3;++k) { low[k]=std::min(low[k],p[k]); high[k]=std::max(high[k],p[k]); } }
        }
        for(unsigned i=0;i<n->mNumChildren;++i) walk(n->mChildren[i],world);
    };
    walk(scene.mRootNode,aiMatrix4x4());
    require(visibleMeshes>0&&visibleTriangles>0,"audit placed visible geometry");
    QJsonObject report{{"source",path},{"source_sha256",QString::fromLatin1(QCryptographicHash::hash(readFile(path),QCryptographicHash::Sha256).toHex())},
        {"mesh_resources",int(scene.mNumMeshes)},{"resource_vertices",double(vertices)},{"resource_triangles",double(triangles)},
        {"nodes",int(nodes)},{"xml_nodes",int(result.occurrencePaths.size())},{"mesh_occurrences",int(countSceneMeshOccurrences(scene.mRootNode))},
        {"visible_mesh_occurrences",int(visibleMeshes)},{"visible_triangles",double(visibleTriangles)},{"occurrence_appearances",appearances},
        {"bounds_min",QJsonArray{low.x,low.y,low.z}},{"bounds_max",QJsonArray{high.x,high.y,high.z}},{"passed",true}};
    QFile file(output); require(file.open(QIODevice::WriteOnly),"audit output"); file.write(QJsonDocument(report).toJson());
    std::cout<<QJsonDocument(report).toJson().constData();
}
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    try {
        synthetic();
        variants();
        sceneVariants();
        if(argc==4&&QString::fromLocal8Bit(argv[1])==QLatin1String("--audit")) audit(QString::fromLocal8Bit(argv[2]),QString::fromLocal8Bit(argv[3]));
        if(argc==2) {
            auto imported=ThreeDXml::Importer::read(QString::fromLocal8Bit(argv[1]),{});
            quint64 triangles=0; for(unsigned i=0;i<imported.scene->mNumMeshes;++i) triangles+=imported.scene->mMeshes[i]->mNumFaces;
            std::cout<<"Imported "<<imported.scene->mNumMeshes<<" unique meshes, "<<triangles<<" resource triangles\n";
        }
        if(argc==5) sample(QString::fromLocal8Bit(argv[1]),QString::fromLocal8Bit(argv[2]),QString::fromLocal8Bit(argv[3]),QString::fromLocal8Bit(argv[4]));
        std::cout<<"3DXML checks passed\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
