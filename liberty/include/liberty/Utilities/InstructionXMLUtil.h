#ifndef LLVM_INSTRUCTION_XML_UTIL_H
#define LLVM_INSTRUCTION_XML_UTIL_H
#include "llvm/IR/Instruction.h"
#include <fstream>
#include <map>

namespace liberty {
  
  using namespace llvm;

  typedef struct {
    const char* special;
    const char* xml;
  } __xmlTy;

  extern const __xmlTy pc_xml_data[]; 

  extern StringRef xml_replace_chars[]; 

  void formatXMLSpecialChars(std::string &str);

  std::string getInstrPositionInBB(Instruction *inst);
  int getInstPositionInBB(Instruction *inst);

    class SourceLineInfo {

    public:
       static const unsigned MAXBUF = 2048;

       std::map<unsigned, char*> lineToSourceStr;
       StringRef file;
       StringRef dir;
       std::string completeFileName;
       std::ifstream fd;
       char *buffer;

    public:
       SourceLineInfo(StringRef d, StringRef f);
       bool openFile();
       char* findLine(unsigned lineNum);
       bool closeFile();
       ~SourceLineInfo();
  };

  typedef std::map<std::pair<StringRef, StringRef>, SourceLineInfo*> 
                                    DirFileToSourceLineInfoMapTy;
  class SourceLineStore {
    private:
      DirFileToSourceLineInfoMapTy dfToInfoMap;

    public:
      SourceLineStore();
      void createSourceLineStore();
      char* findLine(StringRef dir, StringRef fileN, unsigned lineNum);
      void destroySourceLineStore();
      char *getSourceLineForInstruction(Instruction* inst);
      std::string getXMLSourceLineForInstruction(Instruction* inst);
      bool getLineNumInfoForInstruction(Instruction *inst, 
                                          StringRef& Dir, 
                                          StringRef& File,
                                          unsigned& Line);
      
  };


  extern SourceLineStore SrcLineStore;
}
#endif
