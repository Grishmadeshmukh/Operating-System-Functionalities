#include <iostream>
#include <fstream>
#include <cstring>
#include <sstream>
#include <map>
#include <string>
#include <cctype>  
#include <algorithm> 
#include <vector>
#include <iomanip>
using namespace std; 

//------------------------------------------------------------------------------------------------------

static ifstream inputFile;  
map<string, int> symbolTable; 
map<string, pair<int,bool> > symbolUsed; 
vector<int> module_base;       // base addresses for each module
map<string, string> symbolErrors; 
vector<bool> usedInInstruction(0,false);  // each symbol in useList is used

const int MAX_LINE_LENGTH = 1024; 
const int MAX_MACHINE_SIZE = 512;
int currentBase = 0;           // track base address of each module
int moduleNum = 0;             // track current module number
static int lineNum = 0;             
static char line[MAX_LINE_LENGTH];   
static char* nextToken = nullptr;    
static int tokenOffset = 0;   
static int tmp = 0;
static int tmplinecount = 0; 

//----------------------------------------- helper functions ----------------------------------------------


bool readNextLine() {
    if (inputFile.getline(line, MAX_LINE_LENGTH)) {
        tmplinecount++;
        nextToken = strtok(line, " \t\n");
        return true; 
    }
    return false; 
}


void updateTokenOffset(char* token) {
    lineNum = tmplinecount; 
    tokenOffset = token - line + 1; 
}


char* fetchNextToken() {
    char* token = nextToken; 
    if (token != nullptr) {
        updateTokenOffset(token); 
        nextToken = strtok(nullptr, " \t\n"); 
        tmp = tokenOffset + strlen(token);
        return token; 
    }
    nextToken = nullptr; 
    return nullptr; 
}

// Tokenizer
char* getToken() {
    if (nextToken == nullptr) {
        if (!readNextLine()) {
            if (strlen(line) == 0 || line[strlen(line) - 1] == '\n') {
                tokenOffset = tmp; 
                return nullptr; 
            }
            lineNum = tmplinecount; 
            tokenOffset = strlen(line) + 1; 
            return nullptr; 
        }
        if (nextToken == nullptr) {
            tokenOffset = strlen(line) + 1; 
            return getToken(); 
        }
    }
    return fetchNextToken(); 
}


void printError(int ruleNumber, const string& additionalInfo = "", bool inlinePrint = false) {
    std::string errorMessage;
    switch (ruleNumber) {
        case 2:
            errorMessage = " Error: This variable is multiple times defined; first value used"; break;
        case 3:
            errorMessage = " Error: " + additionalInfo + " is not defined; zero used"; break;
        case 6:
            errorMessage = " Error: External operand exceeds length of uselist; treated as relative=0"; break;
        case 8:
            errorMessage = " Error: Absolute address exceeds machine size; zero used"; break;
        case 9:
            errorMessage = " Error: Relative address exceeds module size; relative zero used"; break;
        case 10:
            errorMessage = " Error: Illegal immediate operand; treated as 999"; break;
        case 11:
            errorMessage = " Error: Illegal opcode; treated as 9999"; break;
        case 12:
            errorMessage = " Error: Illegal module operand ; treated as module=0"; break;
        default: break;
    }
    if (inlinePrint) { std::cout << errorMessage; } 
    else { std::cout << errorMessage << std::endl; }

}

void printWarning(int ruleNumber, int moduleNum, const string& symbol = "", int value = 0, int maxLimit = 0, int useListIndex=0) {
    string message;
    switch (ruleNumber) {
        case 4:  // symbol defined but never used
            message = "Warning: Module " + std::to_string(moduleNum) + ": " + symbol + " was defined but never used";
            break;
        case 5:  // symbol exceeds module size
            message = "Warning: Module " + std::to_string(moduleNum) + ": " + symbol + "=" + std::to_string(value) 
                      + " valid=[0.." + std::to_string(maxLimit-1) + "] assume zero relative";
            break;
        case 6:  // symbol redefinition ignored
            message = "Warning: Module " + std::to_string(moduleNum) + ": " +  symbol + " redefinition ignored";
            break;
        case 7:  // uselist symbol not used
            message = "Warning: Module " + std::to_string(moduleNum) + ": " +  "uselist[" +  std::to_string(useListIndex) +"]"+ "="+ symbol + " was not used";
            break;
        case 13:  // uselist symbol referenced multiple times but not used
            message = "Warning: Module " + std::to_string(moduleNum) + ": uselist symbol " + symbol + " was referenced but not used";
            break;
        default:
            message = "Warning: Unknown warning rule encountered.";
            break;
    }
    std::cout << message << std::endl;
}

void parseErrors(int errcode) {
    static const std::string errstr[] = {
        "TOO_MANY_DEF_IN_MODULE",   // > 16 symbol definitions in a module.
        "TOO_MANY_USE_IN_MODULE",   // > 16 uses in a module.
        "TOO_MANY_INSTR",           // total number of instructions > 512.
        "NUM_EXPECTED",             // not a number 
        "SYM_EXPECTED",             // not a symbol 
        "MARIE_EXPECTED",           // !=addressing mode (M/A/R/I/E) 
        "SYM_TOO_LONG"              // symbol name > maximum allowed length.
    };
    std::cout << "Parse Error line " << lineNum << " offset " << tokenOffset << ": " << errstr[errcode] << endl;
    exit(1);  
}

int readInt() {
    char* token = getToken();
    if (token == nullptr) {
        return -1 ;
    }
    for (int i = 0; token[i] != '\0'; i++) {
        if (!isdigit(token[i])) {
            parseErrors(3);  // NUM_EXPECTED: Token is not a valid number
        }
    }
    return atoi(token);
}

char readMARIE() {
    char* token = getToken();  
    if (token == nullptr || strlen(token) != 1) {
        parseErrors(5);  // MARIE_EXPECTED: Token not found or invalid length
    }
    char mode = token[0];  
    if (mode != 'M' && mode != 'A' && mode != 'R' && mode != 'I' && mode != 'E') {
        parseErrors(5);  // MARIE_EXPECTED: Token is not a valid addressing mode
    }
    return mode;  
}

std::string readSym() {
    char* token = getToken();  
    if (token == nullptr) { // no symbol is found
        parseErrors(4);  // SYM_EXPECTED: No token found
    }
    if (!isalpha(token[0])) {
        parseErrors(4);  // SYM_EXPECTED: Token is not a valid symbol
    }
    for (int i = 1; token[i] != '\0'; i++) {
        if (!isalnum(token[i])) {
            parseErrors(4);  // SYM_EXPECTED: Token is not a valid symbol
        }
    }
    return string(token);  
}
//---------------------------------------------------------------------------------------------------

//---------------------------------- Pass 1 helper functon--------------------------------------------
void createSymbolTable(const string& sym, int val, int moduleSize ){    
    if (val > moduleSize) { 
        printWarning(5, moduleNum, sym, val, moduleSize);  
        val = 0;  // assume zero relative if value is larger than module size
    }
    if (symbolTable.find(sym) == symbolTable.end()) {
        symbolTable[sym] = currentBase + val;
        symbolErrors[sym] = "";   
    } 
    else {
        printWarning(6, moduleNum, sym);  // print the warning immediately
        symbolErrors[sym] = " Error: This variable is multiple times defined; first value used"; 
    }
}
//---------------------------------------------------------------------------------------------------

//------------------------------------ Pass 1 -------------------------------------------------------
void Pass1() {
    while (true) {
        // DEF
        int defcount = readInt();
        if (defcount < 0) { break; } // eof
        if (defcount > 16) { parseErrors(0); } // TOO_MANY_DEF_IN_MODULE
        map<string, int> temp; 
        for (int i = 0; i < defcount; i++) {
            string sym = readSym();  
            int val = readInt();    
           temp[sym] = val;
           symbolUsed[sym] = std::make_pair(moduleNum, false); 
        }
   
        // USE (read but ignore)
        int usecount = readInt();
        if (usecount > 16) { parseErrors(1); } // TOO_MANY_USE_IN_MODULE
        for (int i = 0; i < usecount; i++) {
            string sym = readSym();  //  read and discard symbol for now
        }
        
        // INSTRUCTIONS
        int instcount = readInt();
        for (map<string, int>::const_iterator it = temp.begin(); it != temp.end(); ++it) {
            createSymbolTable(it->first, it->second, instcount);
        }
        if (instcount + currentBase > 512) { parseErrors(2); } // TOO_MANY_INSTR
        // skip actual instructions for now
        for (int i = 0; i < instcount; i++) {
            char mode = readMARIE();  
            int operand = readInt();    
        }


        // update base address for the next module
        module_base.push_back(currentBase);
        currentBase += instcount;
        moduleNum++; // for warnings

    }

    std::cout << "Symbol Table" << endl;
    for (map<string, int>::const_iterator it = symbolTable.begin(); it != symbolTable.end(); ++it) {
        std::cout << it->first << "=" << it->second;
        if (!symbolErrors[it->first].empty()) {
            std::cout << symbolErrors[it->first];  
        }
        std::cout << endl;
    }
    std::cout << endl;
}

//---------------------------------------------------------------------------------------------------

//--------------------------------------- Pass 2 helper functons -------------------------------------

void parseM(int operand, int resolvedAddress, int instructionIndex) {
    int moduleIndex = operand % 1000;  
    int opcode = operand / 1000;       

    // if operand references a valid module
    if (moduleIndex >= module_base.size()) {
        //  module index out of range -> eeror and assume base of module 0
        resolvedAddress = opcode * 1000;  // reset operand to zero -> assume base of module 0
        cout << std::setw(3) << std::setfill('0') << instructionIndex << ": " 
             << std::setw(4) << std::setfill('0') << resolvedAddress << " ";
        printError(12);  
    } 
    else {
        resolvedAddress = opcode * 1000 + module_base[moduleIndex];
        cout << std::setw(3) << std::setfill('0') << instructionIndex << ": " 
             << std::setw(4) << std::setfill('0') << resolvedAddress << endl;
    }
}


void parseA(int operand, int resolvedAddress, int instructionIndex){
    if (operand % 1000 >= MAX_MACHINE_SIZE) {
        resolvedAddress = operand / 1000 * 1000;
        cout << std::setw(3) << std::setfill('0') << instructionIndex << ": " 
            << std::setw(4) << std::setfill('0') << resolvedAddress<< " ";
        printError(8);  // bbsolute address > machine size
    } 
    else {
        resolvedAddress = operand;
        cout << std::setw(3) << std::setfill('0') << instructionIndex << ": " 
                << std::setw(4) << std::setfill('0') << resolvedAddress << endl;
    }
}

void parseR(int operand, int resolvedAddress, int instcount, int instructionIndex){
  
    if (operand % 1000 >= instcount) {
        resolvedAddress = (operand / 1000) * 1000 + instructionIndex;
        cout << std::setw(3) << std::setfill('0') << instructionIndex << ": " 
            << std::setw(4) << std::setfill('0') << resolvedAddress<< " ";
        printError(9);  // relative address > module size
    } 
    else {
        resolvedAddress = operand + currentBase;
        cout << std::setw(3) << std::setfill('0') << instructionIndex << ": " 
            << std::setw(4) << std::setfill('0') << resolvedAddress << endl;
    }

}

void parseI(int operand, int resolvedAddress, int instructionIndex){
    if (operand%1000 >= 900) { 
        resolvedAddress = 9999;
        cout << std::setw(3) << std::setfill('0') << instructionIndex << ": " 
            << std::setw(4) << std::setfill('0') << resolvedAddress<< " ";
        printError(10);  // Illegal immediate operand
    }
    else {
        resolvedAddress = operand;
        cout << std::setw(3) << std::setfill('0') << instructionIndex << ": " 
            << std::setw(4) << std::setfill('0') << resolvedAddress << endl;
    }
}

void parseE(int operand, int resolvedAddress, int instructionIndex, int usecount,
            vector<string> useList, int moduleNum){ 
int useIndex = operand % 1000;
    if (useIndex >= usecount) {
        resolvedAddress = operand / 1000 * 1000;
        cout << std::setw(3) << std::setfill('0') << instructionIndex << ": " 
             << std::setw(4) << std::setfill('0') << resolvedAddress<< " ";
        printError(6);  // external operand > length of uselist
    } 
    else {
        string sym = useList[useIndex];
        usedInInstruction[useIndex] = true;

        if (symbolTable.find(sym) != symbolTable.end()) {
            resolvedAddress = operand / 1000 * 1000 + symbolTable[sym];
            symbolUsed[sym] = std::make_pair(moduleNum, true);
            cout << std::setw(3) << std::setfill('0') << instructionIndex << ": " 
                   << std::setw(4) << std::setfill('0') << resolvedAddress << endl;
        } 
        else {
            resolvedAddress = operand / 1000 * 1000;
            cout << std::setw(3) << std::setfill('0') << instructionIndex << ": " 
                 << std::setw(4) << std::setfill('0') << resolvedAddress<< " ";
            printError(3, sym);  // Symbol not defined
        }
    }
}

int checkOpcode(int opcode, int instructionIndex) {
    if (opcode < 0 || opcode > 9) {  
        cout << std::setw(3) << std::setfill('0') << instructionIndex << ": " 
                 << std::setw(4) << std::setfill('0') << 9999<< " ";
        printError(11);  // illegal opcode
        return 9999;  
    }
    return opcode;
}

//----------------------------------------------------------------------------------------------------
//------------------------------------ Pass 2 --------------------------------------------------------
void Pass2() {
    int instructionIndex = 0;
    cout << "Memory Map" << endl;

    while (true) {
        // DEF (read but ignore)
        int defcount = readInt();
        if (defcount < 0) { break; }  // eof
        for (int i = 0; i < defcount; i++) {
            string sym = readSym();
            int val = readInt();
        }
        
        // USE
        int usecount = readInt();
        vector<string> useList(usecount);      
        usedInInstruction.resize(usecount, false); 
        for (int i = 0; i < usecount; i++) {
            useList[i] = readSym();
        }

        // INSTRUCTIONS
        int instcount = readInt();
        for (int i = 0; i < instcount; i++) {
            char mode = readMARIE();
            int operand = readInt();
            int resolvedAddress = 0;
            int opcode = operand / 1000;  
            int checkedOpcode = checkOpcode(opcode,instructionIndex);  
    
            switch (mode) {
                case 'R':  
                    if (checkedOpcode == 9999)  resolvedAddress = checkedOpcode;
                    else parseR(operand, resolvedAddress, instcount, instructionIndex);
                    break;
                case 'I': 
                    if (checkedOpcode == 9999)  resolvedAddress = checkedOpcode;
                    else parseI(operand, resolvedAddress, instructionIndex);
                    break;
                case 'E': 
                    if (checkedOpcode == 9999)  resolvedAddress = checkedOpcode;
                    else parseE(operand, resolvedAddress, instructionIndex, usecount, useList, moduleNum);
                    break;
                case 'A':  
                    if (checkedOpcode == 9999)  resolvedAddress = checkedOpcode;
                    else parseA(operand, resolvedAddress, instructionIndex);
                    break;
                 case 'M': 
                    if (checkedOpcode == 9999)  resolvedAddress = checkedOpcode;
                    else parseM(operand, resolvedAddress, instructionIndex);
                    break;
                default:
                    cout << "Unknown addressing mode: " << mode << endl;
                    break;
            }
            instructionIndex++;
        }

        //symbols in  use list ->  not used in the current module
        for (int i = 0; i < usecount; i++) {
            if (!usedInInstruction[i]) {
                printWarning(7, moduleNum, useList[i], 0,0,i);  //uselist symbol not used
            }
        }
        currentBase += instcount;
        moduleNum++;
    }
    cout<<""<<endl;
    for (std::map<std::string, std::pair<int, bool> >::iterator it = symbolUsed.begin(); it != symbolUsed.end(); ++it) {
        int modn = it->second.first;  
        bool used = it->second.second;      

        if (!used) {
            printWarning(4, modn, it->first, 0, 0);  
        }
    }
    cout<<""<<endl;
}
//---------------------------------------------------------------------------------------------------

//------------------------------------------ MAIN ---------------------------------------------------

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <input_file>" << endl;
        return 1; 
    }
 
    inputFile.open(argv[1]);
    if (!inputFile.is_open()) {
        cerr << "Could not open file " << argv[1] << endl;
        exit(1);
    }
    
    Pass1();

    // reset for Pass2
    currentBase = 0;
    moduleNum = 0;
    inputFile.clear();  // clear EOF flag
    inputFile.seekg(0, ios::beg);  // file pointer -> beginning

    Pass2();

    inputFile.close(); 
    return 0;
}





