#pragma once

inline ifc730::Entities volumeEvidenceCube(double offset=0)
{
    ifc730::Entities all;
    const std::vector<ifc730::V> points{{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}};
    for(unsigned i=0;i<points.size();++i)
    {
        std::ostringstream text;text<<std::setprecision(17)<<'('<<points[i][0]+offset<<','<<points[i][1]+offset<<','<<points[i][2]+offset<<')';
        all[i+1]={"IFCCARTESIANPOINT",{text.str()}};
    }
    const std::vector<std::string> loops{"(#1,#4,#3,#2)","(#5,#6,#7,#8)","(#1,#2,#6,#5)","(#2,#3,#7,#6)","(#3,#4,#8,#7)","(#4,#1,#5,#8)"};
    for(unsigned i=0;i<loops.size();++i)
    {
        all[20+i]={"IFCPOLYLOOP",{loops[i]}};
        all[30+i]={"IFCFACEOUTERBOUND",{"#"+std::to_string(20+i),".T."}};
        all[40+i]={"IFCFACE",{"(#"+std::to_string(30+i)+")"}};
    }
    all[50]={"IFCCLOSEDSHELL",{"(#40,#41,#42,#43,#44,#45)"}};
    all[51]={"IFCFACETEDBREP",{"#50"}};
    all[52]={"IFCSHAPEREPRESENTATION",{"$","'Body'","'Brep'","(#51)"}};
    all[53]={"IFCPRODUCTDEFINITIONSHAPE",{"$","$","(#52)"}};
    return all;
}

inline int volumeEvidenceRegression()
{
    using namespace ifc730;
    try
    {
        auto original=volumeEvidenceCube();unsigned rejected=0;
        require(std::abs(facetedVolume(original,53)-1)<1e-12,"unit cube volume control");
        require(std::abs(facetedVolume(volumeEvidenceCube(1e9),53)-1)<1e-12,"translated cube volume control");
        auto reversedLoop=original;reversedLoop[20].args[0]="(#2,#3,#4,#1)";reversedLoop[30].args[1]=".F.";
        require(std::abs(facetedVolume(reversedLoop,53)-1)<1e-12,"bound orientation control");
        const auto rejects=[&](Entities altered,const std::string& diagnostic) {
            bool caught=false;
            try{(void)facetedVolume(altered,53);}
            catch(const std::exception& e){caught=std::string(e.what()).find(diagnostic)!=std::string::npos;}
            require(caught,"mutation not rejected as expected: "+diagnostic);++rejected;
        };
        auto bad=original;bad[50].args[0]="(#40,#41,#42,#43,#44)";rejects(bad,"open or inconsistently oriented");
        bad=original;bad[50].args[0]="(#40,#41,#42,#43,#44,#45,#45)";rejects(bad,"duplicate volume face");
        bad=original;bad[30].args[1]=".F.";rejects(bad,"repeated directed volume edge");
        bad=original;bad[30].args[1]=".UNKNOWN.";rejects(bad,"volume bound orientation");
        bad=original;bad[7].args[0]="(1,1,1.01)";rejects(bad,"nonplanar volume face");
        bad=original;bad[20].args[0]="(#1,#4,#3,#2,#1)";rejects(bad,"volume loop vertices");
        bad=original;bad[2]=bad[1];rejects(bad,"zero length volume edge");
        bad=original;for(unsigned i=30;i<36;++i)bad[i].args[1]=".F.";rejects(bad,"nonpositive oriented shell volume");
        bad=original;bad[52].args[3]="(#51,#51)";rejects(bad,"duplicate volume BREP");
        bad=original;bad[30].type="IFCFACEBOUND";rejects(bad,"one outer bound");
        bad=original;bad[53].args[2]="(#52,#52)";rejects(bad,"duplicate volume representation");
        bad=original;bad[54]=bad[51];bad[52].args[3]="(#51,#54)";rejects(bad,"repeated volume shell");
        require(rejected==12,"volume evidence mutation coverage");
        std::cout<<"volume_evidence_controls=3 rejected_mutations="<<rejected<<'\n';return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
