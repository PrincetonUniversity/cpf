#include <sstream>
#include <string>
#include <map>
#include <vector>
#include <algorithm>
using std::string, std::map, std::vector;

enum ReplAction {
  Help = 0,
  Loops,
  Select,
  Quit,
  Dump,
  Insts,
  Deps,
  Remove,
  Parallelize,
  Modref,
  Unknown = -1
};

const map<string, ReplAction> ReplActions = {
  {"help", ReplAction::Help},
  {"h", ReplAction::Help},
  {"loops", ReplAction::Loops},
  {"ls", ReplAction::Loops},
  {"select", ReplAction::Select},
  {"s", ReplAction::Select},
  {"quit", ReplAction::Quit},
  {"q", ReplAction::Quit},
  {"dump", ReplAction::Dump},
  {"d", ReplAction::Dump},
  {"insts", ReplAction::Insts},
  {"is", ReplAction::Insts},
  {"deps", ReplAction::Deps},
  {"ds", ReplAction::Deps},
  {"remove", ReplAction::Remove},
  {"r", ReplAction::Remove},
  {"parallelize", ReplAction::Parallelize},
  {"p", ReplAction::Parallelize},
  {"modref", ReplAction::Modref},
};

// get all names
const vector<string> ReplActionNames = [](map<string, ReplAction> map) {
  vector<string> v;
  for (auto &[s, a] : map) {
    v.push_back(s);
  }
  v.push_back("from");
  v.push_back("to");
  return v;
}(ReplActions);

const map<ReplAction, string> HelpText = {
  {Help, "help/h (command): \tprint help message (for certain command)"},
  {Loops, "loops/ls: \tprint all loops with loop id"},
  {Select, "select/s \t$loop_id: select a loop to work with"},
  {Dump, "dump (-v):\t dump the loop information (verbose: dump the loop instructions)"},
  {Insts, "insts/is: \tshow instructions with instruction id"},
  {Deps, "deps/ds (from $inst_id_from) (to $inst_id_to): \tshow dependences with dependence id (from or to certain instructions)"},
  {Remove, "remove/r $dep_id: \tremove a certain dependence from the loop"},
  {Parallelize, "paralelize/p: \tparallelize the selected loop with current dependences"},
  {Modref, "modref/mr $inst_id1, $inst_id2: \tquery the modref between two instructions"},
  {Quit, "quit/q: quit the repl"},
};

class ReplParser {

  private:
    string originString;
    static bool isNumber(const string& s)
    {
      for (char const &c : s) {
        if (std::isdigit(c) == 0)
          return false;
      }
      return true;
    }

    int getQueryKeyword(string query) {
      auto pos= originString.find(query);
      if (pos == string::npos)
        return -1;

      pos += query.size() + 1; // move to the start of the number (+1 is the space between)
      if (pos >= originString.size()) 
        return -1;
      // get the number
      string number = originString.substr(pos, originString.find(" ", pos) - pos);
      if (!isNumber(number)) {
        return -1;
      }
      else {
        return stoi(number);
      }
    }

  public: 
    ReplParser(string str): originString(str) {

    }

    // get the action for the command
    ReplAction getAction() {
      string firstWord;
      firstWord = originString.substr(0, originString.find(" ")); //first space or end

      if (ReplActions.find(firstWord) != ReplActions.end()) {
        return ReplActions.at(firstWord);
      }
      else {
        return ReplAction::Unknown;
      }
    }

    string getStringAfterAction() {
      auto secondWordStartPos = originString.find(" ");
      if (secondWordStartPos == string::npos) {
        return "";
      }
      
      secondWordStartPos += 1;

      auto secondWordLen = originString.find(" ", secondWordStartPos) - secondWordStartPos;
      string secondStr = originString.substr(secondWordStartPos, secondWordLen);
      return secondStr;
    }

    // the number of action
    int getActionId() {
      string firstWord;
      firstWord = originString.substr(0, originString.find(" ")); //first space or end

      return getQueryKeyword(firstWord);
    }

    // the number after from
    int getFromId() {
      string query = "from";
      return getQueryKeyword(query);
    }

    // the number after to
    int getToId() {
      string query = "to";
      return getQueryKeyword(query);
    }

    bool isVerbose() {
      if (originString.find("-v") != string::npos) {
        return true;
      } else {
        return false;
      }
    };
};
