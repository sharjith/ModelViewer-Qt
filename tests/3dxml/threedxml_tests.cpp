#include "Importer.h"
#include "ArchiveReader.h"
#include "ProductStructureReader.h"
#include "RepresentationReader.h"
#include "PackedReader.h"
#include "../../include/Import/SceneImportCounts.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <minizip/zip.h>
#include <QtEndian>
#include <iostream>
#include <cmath>
#include <map>

using namespace ThreeDXml;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class Fn> void rejects(Fn fn) { bool rejected=false; try { fn(); } catch (const Error&) { rejected=true; } require(rejected,"expected checked rejection"); }
QByteArray readFile(const QString& path) { QFile f(path); require(f.open(QIODevice::ReadOnly),"cannot open reference file"); return f.readAll(); }
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

void synthetic()
{
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
        auto found=primitives.find(QString::fromUtf8(mesh.mName.C_Str())+'/'+QString::fromUtf8(name.C_Str()));
        if(found==primitives.end()) continue; // GLB intentionally omitted hidden-only resources.
        ++compared; auto attrs=found->second["attributes"].toObject();
        const auto pbr=mats[found->second["material"].toInt()].toObject()["pbrMetallicRoughness"].toObject();
        const auto rgba=pbr["baseColorFactor"].toArray(); aiColor4D color;
        require(scene.mMaterials[mesh.mMaterialIndex]->Get(AI_MATKEY_BASE_COLOR,color)==AI_SUCCESS,"base color property");
        for(int k=0;k<4;++k) require(std::abs(color[k]-rgba[k].toDouble())<1e-7,"material color parity");
        int position=attrs["POSITION"].toInt(),normal=attrs["NORMAL"].toInt();
        require(accessors[position].toObject()["count"].toInt()==int(mesh.mNumFaces*3),"primitive triangle count");
        for(unsigned f=0;f<mesh.mNumFaces;++f) for(unsigned c=0;c<3;++c) {
            auto i=mesh.mFaces[f].mIndices[c];
            require((mesh.mVertices[i]-vector(position,f*3+c)).SquareLength()==0,"source position parity");
            require((mesh.mNormals[i]-vector(normal,f*3+c)).Length()<1e-6,"source normal parity");
        }
    }
    require(compared==int(primitives.size()),"all reference primitives checked");
    require(xmlNodes==252 && hiddenXml==17 && visibleBodies==276 && visibleMeshes==538 && visibleTriangles==5608912,"sample scene counts");
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
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    try {
        synthetic();
        if(argc==5) sample(QString::fromLocal8Bit(argv[1]),QString::fromLocal8Bit(argv[2]),QString::fromLocal8Bit(argv[3]),QString::fromLocal8Bit(argv[4]));
        std::cout<<"3DXML checks passed\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
