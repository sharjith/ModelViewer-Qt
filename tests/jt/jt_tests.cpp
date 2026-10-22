#include "Jt/JtImporter.h"
#include <JtData_Reader.hxx>
#include <JtData_Limits.hxx>
#include <JtDecode_Int32CDP.hxx>
#include <JtDecode_VertexColors.hxx>
#include <JtElement_ShapeLOD_Vertex.hxx>
#include "JtContainerFixture.h"
#include <JtAttribute_Material.hxx>
#include <JtAttribute_GeometricTransform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/material.h>
#include <cmath>
#include <iostream>
#include <vector>

void materialOrderFixtureFiles(const QString& folder);
void jtMaterialMaskFixtureFiles(const QString& folder);
void jt95Tests();
void jt1010FixtureFiles(const QString& folder);
void jt107108FixtureFiles(const QString& folder);
void jt105FixtureFiles(const QString& folder);
void jt90FixtureFiles(const QString& folder);
void jt95FixtureFiles(const QString& folder);
void solidEdgeMaterialCompatibilityTests();
void paletteTests();
void paletteFixtureFiles(const QString& folder);
void modernAttributeTests();
void modernAttributeFixtureFiles(const QString& folder);
void sceneTests();
void vertexAlphaFixtureFiles(const QString& folder);
void textureTests();
void textureTransformTests();
void texturePlanarTests();
void texturePixelTests();
void jt9TextureTests();
void textureBorderTests();
void constantBorderTests();
void explicitBorderTests();
void borderFormatTests();
void textureFixtureFiles(const QString& folder);
void containerTests(const QString& samples);
void legacyTests();
void huffmanFixtureFiles(const QString& folder);
void jt9ColourTests();
void uvTests();
void coordinateTests();
void quantizedColourTests();
void huffmanTests();
void arithmeticCountTests();
void arithmeticCountFixtureFiles(const QString& folder);
void quantizedTests();
void quantizedFixtureFiles(const QString& folder);
void legacyFixtureFiles(const QString& folder);
void warehouseTests(const QString& path);

namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class F> void fails(F action) {bool failed=false;try{action();}catch(const std::exception&){failed=true;}require(failed,"Expected rejected input");}
struct TestModel: JtData_Model {TestModel(bool big=false):JtData_Model(TCollection_ExtendedString()) {myMajorVersion=10;myMinorVersion=0;myIsFileLE=!big;}};
struct Reader: JtData_Reader {
    std::vector<uint8_t> bytes;size_t pos=0;
    explicit Reader(std::vector<uint8_t> b,bool big=false):JtData_Reader(new TestModel(big)),bytes(std::move(b)){}
    Standard_Boolean ReadBytes(void* out,Standard_Size count) override {if(count>bytes.size()-pos)return false;if(count)std::memcpy(out,bytes.data()+pos,count);pos+=count;return true;}
    Standard_Boolean SkipBytes(Standard_Size count) override {if(count>bytes.size()-pos)return false;pos+=count;return true;}
    Standard_Size GetPosition()const override{return pos;}
};
void u32(std::vector<uint8_t>& b,uint32_t n) {for(int i=0;i<4;++i)b.push_back(uint8_t(n>>(8*i)));}
void f32(std::vector<uint8_t>& b,float value) {uint32_t bits;std::memcpy(&bits,&value,4);u32(b,bits);}
void allocationBudgetTests() {
    unsigned polls=0;
    JtData_Limits::Scope outer([&]{++polls;},100);
    JtData_Limits::allocation(60);
    fails([]{JtData_Limits::allocation(41);});
    require(JtData_Limits::allocated==60,"Rejected allocation consumed budget");
    {
        JtData_Limits::Scope inner([]{},5);
        JtData_Limits::allocation(5);fails([]{JtData_Limits::allocation(1);});
    }
    require(JtData_Limits::allocated==60 && JtData_Limits::allocationBudget==100,"Nested budget scope lost parent accounting");
    JtData_Limits::allocation(40);require(polls==3,"Nested scope lost cancellation callback");
    fails([]{JtData_Limits::allocation(size_t(-1));});
    {JtData_Limits::Scope limit([]{});fails([]{JtData_Limits::allocation(size_t(256)*1024*1024+1);});}
    {JtData_Limits::Scope cancel([]{throw Jt::Cancelled();});bool cancelled=false;
     try{JtData_Limits::allocation(1);}catch(const Jt::Cancelled&){cancelled=true;}
     require(cancelled && JtData_Limits::allocated==0,"Budget accounting swallowed cancellation");}
}
void attributeTests() {
    for(bool blending:{false,true}) {
        // JT 10 base version/state, field-inhibit and field-final words.
        std::vector<uint8_t> b{1,8};u32(b,0);u32(b,0x01234567);
        b.push_back(1);b.push_back(blending ? 0x10 : 0);b.push_back(0);
        for(float v:{0.2f,0.2f,0.2f,1.0f, 0.3f,0.5f,0.7f,0.25f,
                     0.75f,0.75f,0.75f,1.0f, 0.0f,0.0f,0.0f,1.0f, 15.0f}) f32(b,v);
        f32(b,0.0f); f32(b,1.0f); // JT 10 reflectivity and bumpiness
        Reader reader(b);JtAttribute_Material material;
        require(material.Read(reader) && reader.pos==b.size(),"JT material read failed or fields left unread");
        const auto c=material.DiffuseColor();
        require(c[0]==0.3f && c[1]==0.5f && c[2]==0.7f && c[3]==0.25f,"JT material header shifted RGBA fields");
        require(material.BlendingEnabled()==blending,"JT blending flag mismatch");
        require(material.Shininess()==15.0f,"JT material exponent read incorrectly");
        auto bad=b;bad[0]=3;Reader unknownBase(bad);JtAttribute_Material unsupported;
        fails([&]{unsupported.Read(unknownBase);});
        bad=b;bad[10]=2;Reader unknownMaterial(bad);fails([&]{unsupported.Read(unknownMaterial);});
    }
    std::vector<uint8_t> b{1,8};u32(b,0);u32(b,0x01234567);
    b.insert(b.end(),{1,0,0}); // Transform version and identity-mask.
    Reader reader(b);JtAttribute_GeometricTransform transform;
    require(transform.Read(reader) && reader.pos==b.size(),"JT transform base header mismatch");
    const auto t=transform.GetTrsf();
    for(int i=0;i<16;++i) require(t[i]==(i%5==0 ? 1.0 : 0.0),"JT identity transform mismatch");
}
std::vector<uint8_t> plain(std::initializer_list<int32_t> values) {
    std::vector<uint8_t> b;u32(b,uint32_t(values.size()));if(values.size()){b.push_back(0);u32(b,uint32_t(values.size()*32));for(auto v:values)u32(b,uint32_t(v));}return b;
}
std::vector<int32_t> decode(std::vector<uint8_t> bytes) {
    Reader r(std::move(bytes));JtDecode_Int32CDP packet;require(packet.Load2(r),"Packet load failed");Jt_VecI32 values(packet.DecodeI32());
    std::vector<int32_t> out;for(int i=0;i<values.Count();++i)out.push_back(values[i]);require(r.pos==r.bytes.size(),"Unexpected packet tail");return out;
}
void codecTests() {
    require(decode(plain({0,-1,123456}))==std::vector<int32_t>({0,-1,123456}),"Null codec mismatch");
    std::vector<uint8_t> b;u32(b,5);b.push_back(5);const auto values=plain({7,8,9}), offsets=plain({-1,-1,1,-1,2});b.insert(b.end(),values.begin(),values.end());b.insert(b.end(),offsets.begin(),offsets.end());
    require(decode(b)==std::vector<int32_t>({7,8,7,9,8}),"Move-to-front mismatch");
    // Fixed-width mark-3 stream: mode=0, signed nibble minimum=-2,
    // maximum=3, then three unsigned offsets 0,2,5 using three bits each.
    const std::string bits="0" "11100" "00110" "000" "010" "101";
    std::vector<uint8_t> fixed;u32(fixed,3);fixed.push_back(1);u32(fixed,uint32_t(bits.size()));
    uint32_t word=0;for(size_t i=0;i<bits.size();++i)word|=uint32_t(bits[i]=='1')<<(31-i);u32(fixed,word);
    require(decode(fixed)==std::vector<int32_t>({-2,0,3}),"Mark-3 bitlength mismatch");
    fails([&]{auto bad=b;bad.pop_back();decode(bad);});
    fails([]{std::vector<uint8_t> bad;u32(bad,0xffffffff);decode(bad);});
    fails([]{decode(plain({1,2,3})); JtData_Vector<int> bad(-1);});
    fails([]{JtData_Limits::Scope scope([]{});JtData_Limits::allocation(size_t(2)*1024*1024*1024);});
    Jt::Options cancelled;cancelled.cancelled=[]{return true;};
    fails([&]{Jt::Importer::read("missing.jt",cancelled);});
}
void colorStreamTests() {
    for(bool big:{false,true}) {
        Fixture::Bytes raw(big);raw.u64(0x3a);std::vector<uint8_t> bytes(raw.data.begin(),raw.data.end());Reader reader(bytes,big);
        JtElement_ShapeLOD_Vertex::VertexBinding2 binding;
        require(binding.Read(reader),"Cannot read colour binding");fails([&]{binding.NbColorComponents();});
    }
    for(bool big:{false,true}) for(unsigned components:{3u,4u}) {
        Fixture::Bytes stream(big);stream.u32(6);stream.u8(components);stream.u8(0);
        float expected[6][4]{};
        for(unsigned c=0;c<components;++c) {
            stream.u32(6);stream.u8(0);stream.u32(6*32);
            uint32_t previous=0;
            for(unsigned i=0;i<6;++i) {
                const float value=c==3?1.0f:float((i+c)%5)/4;expected[i][c]=value;
                uint32_t bits;std::memcpy(&bits,&value,4);stream.u32(i<4?bits:bits-previous);previous=bits;
            }
        }
        stream.u32(0);std::vector<uint8_t> bytes(stream.data.begin(),stream.data.end());
        Reader reader(bytes,big);JtDecode_VertexData::Decoded decoded(JtDecode_VertexColors::Read(reader,components,6));
        require(decoded.Count()==6 && decoded.CompCount()==int(components) && reader.pos==bytes.size(),"Colour stream layout/consumption mismatch");
        for(int i=0;i<6;++i) for(unsigned c=0;c<components;++c) require(decoded[i][c]==expected[i][c],"Colour predictor/endian mismatch");
        for(size_t size=0;size<bytes.size();++size) fails([&]{Reader truncated(std::vector<uint8_t>(bytes.begin(),bytes.begin()+size),big);JtDecode_VertexColors::Read(truncated,components,6);});
        fails([&]{Reader r(bytes,big);JtDecode_VertexColors::Read(r,components,5);});
        auto bad=bytes;bad[5]=8;fails([&]{Reader r(bad,big);JtDecode_VertexColors::Read(r,components,6);});
        bad=bytes;bad[4]=2;fails([&]{Reader r(bad,big);JtDecode_VertexColors::Read(r,components,6);});
        for(uint32_t bits:{0x7fc00000u,0x7f800000u,0xbf800000u,0x40000000u}) {
            bad=bytes;for(unsigned j=0;j<4;++j)bad[15+j]=uint8_t(bits>>(8*(big?3-j:j)));
            fails([&]{Reader r(bad,big);JtDecode_VertexColors::Read(r,components,6);});
        }
    }
    std::cout<<"Lossless RGB/RGBA colour packets, prediction, endianness and truncation passed\n";
}
unsigned occurrences(const aiNode* n) {unsigned count=n->mNumMeshes;for(unsigned i=0;i<n->mNumChildren;++i)count+=occurrences(n->mChildren[i]);return count;}
void samples(const QString& dir) {
    containerTests(dir);
    const char* names[]={"Bolt","Brake_Disk","Caliper","Rim","Tire"};
    const unsigned expectedTriangles[]={904,13324,1400,14156,16018};
    const unsigned expectedVertices[]={913,13146,1632,10526,16301};
    for(int k=0;k<5;++k) {
        const QString path=QDir(dir).filePath(QString::fromLatin1(names[k])+".jt");
        auto stored=Jt::Importer::read(path);require(stored.scene->mNumMeshes==1,"LOD selection duplicated geometry");
        require(occurrences(stored.scene->mRootNode)==1,"Occurrence count mismatch");
        require(stored.scene->mMeshes[0]->mNumFaces==expectedTriangles[k],"Unexpected decoded triangle count");
        require(stored.scene->mMeshes[0]->mNumVertices==expectedVertices[k],"Unexpected decoded vertex count");
        require(stored.fallbackParts==1 && stored.exactParts==0,"Missing per-part tessellation fallback");
        const auto& mesh=*stored.scene->mMeshes[0];
        const auto& material=*stored.scene->mMaterials[mesh.mMaterialIndex];
        float opacity=0;aiColor4D diffuse;
        require(material.Get(AI_MATKEY_OPACITY,opacity)==AI_SUCCESS && opacity==1.0f,"Opaque source material became transparent");
        require(material.Get(AI_MATKEY_COLOR_DIFFUSE,diffuse)==AI_SUCCESS && diffuse.a==1.0f,"JT diffuse alpha mismatch");
        float exponent=0,roughness=0;
        require(material.Get(AI_MATKEY_SHININESS,exponent)==AI_SUCCESS && exponent>=1 && exponent<=128,"Sample shininess missing or invalid");
        require(material.Get(AI_MATKEY_ROUGHNESS_FACTOR,roughness)==AI_SUCCESS && std::abs(roughness-std::sqrt(2.0f/(exponent+2)))<1e-6f,"Sample roughness mapping failed");
        const float expectedRGB[5][3]={{0.87449455f,0.87449455f,0.87449455f},{0.5960632f,0.6666667f,0.6862593f},{0.5960632f,0.6666667f,0.6862593f},{0.5960632f,0.6666667f,0.6862593f},{0.49019608f,0.49019608f,0.49019608f}};
        require(std::abs(diffuse.r-expectedRGB[k][0])<1e-6f && std::abs(diffuse.g-expectedRGB[k][1])<1e-6f && std::abs(diffuse.b-expectedRGB[k][2])<1e-6f,"JT diffuse RGB mismatch");
        for(unsigned i=0;i<mesh.mNumVertices;++i)require(std::abs(mesh.mNormals[i].Length()-1)<1e-4,"Invalid stored normal");
        bool called=false;
        Jt::Options rejected;rejected.exactDecoder=[&](const QByteArray& bytes)->TopoDS_Shape {called=true;require(bytes.startsWith("PS"),"Invalid extracted XT payload");throw std::runtime_error("Deliberately rejected exact backend");};
        auto recovered=Jt::Importer::read(path,rejected);require(called,"Exact backend was not invoked");require(recovered.fallbackParts==1 && recovered.scene->mMeshes[0]->mNumFaces==expectedTriangles[k],"Exact failure discarded fallback");
        Jt::Options invalid;invalid.exactDecoder=[](const QByteArray&){return TopoDS_Shape();};
        auto invalidRecovered=Jt::Importer::read(path,invalid);require(invalidRecovered.fallbackParts==1,"Invalid B-Rep did not fall back");
        if(k==0) {
            Jt::Options valid;valid.exactDecoder=[](const QByteArray&){return BRepPrimAPI_MakeBox(1,2,3).Shape();};
            auto exact=Jt::Importer::read(path,valid);
            if(exact.exactParts!=1) for(const auto& warning:exact.warnings) std::cerr<<warning.toStdString()<<'\n';
            require(exact.exactParts==1 && exact.fallbackParts==0 && exact.storedMeshes==0,"Accepted exact geometry duplicated stored mesh");
            require(exact.scene->mMeshes[0]->mNumFaces==12,"Exact triangulation mismatch");
            QTemporaryDir temp;Assimp::Exporter exporter;const auto glb=QDir(temp.path()).filePath("roundtrip.glb");
            require(exporter.Export(stored.scene.get(),"glb2",glb.toStdString())==AI_SUCCESS,"GLB validation export failed");
            Assimp::Importer reader;const auto scene=reader.ReadFile(glb.toStdString(),0);require(scene && scene->mNumMeshes==1 && scene->mMeshes[0]->mNumFaces==expectedTriangles[0],"GLB scene roundtrip failed");
            float restoredRoughness=0;
            require(scene->mMaterials[scene->mMeshes[0]->mMaterialIndex]->Get(AI_MATKEY_ROUGHNESS_FACTOR,restoredRoughness)==AI_SUCCESS && std::abs(restoredRoughness-roughness)<1e-6f,"GLB roundtrip lost authored roughness");
            unsigned checks=0;Jt::Options cancel;cancel.cancelled=[&]{return ++checks>100;};fails([&]{Jt::Importer::read(path,cancel);});
        }
        std::cout<<names[k]<<": geometry, normals and B-Rep failure fallback passed\n";
    }
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {allocationBudgetTests();codecTests();colorStreamTests();uvTests();coordinateTests();quantizedColourTests();jt9ColourTests();attributeTests();sceneTests();containerTests({});legacyTests();huffmanTests();arithmeticCountTests();quantizedTests();modernAttributeTests();paletteTests();solidEdgeMaterialCompatibilityTests();jt95Tests();
        textureTests();
        textureTransformTests();
        texturePlanarTests();
        texturePixelTests();
        jt9TextureTests();
        textureBorderTests();
        constantBorderTests();
        explicitBorderTests();
        borderFormatTests();
        const bool textureFixtures=argc==3 && QString::fromLocal8Bit(argv[1])=="--texture-fixtures";
        const bool alphaFixtures=argc==3 && QString::fromLocal8Bit(argv[1])=="--vertex-alpha-fixtures";
        const bool orderFixtures=argc==3 && QString::fromLocal8Bit(argv[1])=="--material-order-fixtures";
        const bool maskFixtures=argc==3 && QString::fromLocal8Bit(argv[1])=="--material-mask-fixtures";
        const bool arithmeticFixtures=argc==3 && QString::fromLocal8Bit(argv[1])=="--arithmetic-count-fixtures";
        const bool huffmanFixtures=argc==3 && QString::fromLocal8Bit(argv[1])=="--huffman-fixtures";
        const bool jt1010Fixtures=argc==3 && QString::fromLocal8Bit(argv[1])=="--1010-fixtures";
        const bool jt107108Fixtures=argc==3 && QString::fromLocal8Bit(argv[1])=="--107108-fixtures";
        const bool jt105Fixtures=argc==3 && QString::fromLocal8Bit(argv[1])=="--105-fixtures";
        const bool jt90Fixtures=argc==3 && QString::fromLocal8Bit(argv[1])=="--90-fixtures";
        const bool jt95Fixtures=argc==3 && QString::fromLocal8Bit(argv[1])=="--95-fixtures";
        const bool fixtures=argc==3 && QString::fromLocal8Bit(argv[1])=="--legacy-fixtures";
        const bool quantizedFixtures=argc==3 && QString::fromLocal8Bit(argv[1])=="--quantized-fixtures";
        const bool modernFixtures=argc==3 && QString::fromLocal8Bit(argv[1])=="--modern-attribute-fixtures";
        const bool paletteFixtures=argc==3 && QString::fromLocal8Bit(argv[1])=="--palette-fixtures";
        if(textureFixtures)textureFixtureFiles(QString::fromLocal8Bit(argv[2]));
        else if(alphaFixtures)vertexAlphaFixtureFiles(QString::fromLocal8Bit(argv[2]));
        else if(arithmeticFixtures)arithmeticCountFixtureFiles(QString::fromLocal8Bit(argv[2]));
        else if(huffmanFixtures)huffmanFixtureFiles(QString::fromLocal8Bit(argv[2]));
        else if(orderFixtures)materialOrderFixtureFiles(QString::fromLocal8Bit(argv[2]));
        else if(maskFixtures)jtMaterialMaskFixtureFiles(QString::fromLocal8Bit(argv[2]));
        else if(jt1010Fixtures)jt1010FixtureFiles(QString::fromLocal8Bit(argv[2]));
        else if(jt107108Fixtures)jt107108FixtureFiles(QString::fromLocal8Bit(argv[2]));
        else if(jt105Fixtures)jt105FixtureFiles(QString::fromLocal8Bit(argv[2]));
        else if(jt90Fixtures)jt90FixtureFiles(QString::fromLocal8Bit(argv[2]));
        else if(jt95Fixtures)jt95FixtureFiles(QString::fromLocal8Bit(argv[2]));
        else if(paletteFixtures)paletteFixtureFiles(QString::fromLocal8Bit(argv[2]));
        else if(modernFixtures)modernAttributeFixtureFiles(QString::fromLocal8Bit(argv[2]));
        else if(quantizedFixtures)quantizedFixtureFiles(QString::fromLocal8Bit(argv[2]));
        else if(fixtures)legacyFixtureFiles(QString::fromLocal8Bit(argv[2]));
        else {if(argc>=2)samples(QString::fromLocal8Bit(argv[1]));if(argc==3)warehouseTests(QString::fromLocal8Bit(argv[2]));}
        std::cout<<"JT tests passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
