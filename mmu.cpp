#include <iostream>
#include <stdlib.h>
#include <string>
#include <vector>
#include <deque>
#include <fstream>
#include <sstream>
#include <getopt.h>
#include "bithacks.h"
#include <climits>

using namespace std;

//FIFO, Random, Clock, NRU

//-------------------------------------------------------------------------------------------------------------

const int PTE_ENTRIES = 64; 
int MAX_NUM_FRAMES = 128;         

deque<int> free_frames;
int instruction_counter = 0;
int current_process_number = 0;

const unsigned long long COST_READ_WRITE = 1;
const unsigned long long COST_CTX_SWITCH = 130;
const unsigned long long COST_PROC_EXIT = 1230;
const unsigned long long COST_MAP = 350;
const unsigned long long COST_UNMAP = 410;
const unsigned long long COST_IN = 3200;
const unsigned long long COST_OUT = 2750;
const unsigned long long COST_FIN = 2350;
const unsigned long long COST_FOUT = 2800;
const unsigned long long COST_ZERO = 150;
const unsigned long long COST_SEGV = 440;
const unsigned long long COST_SEGPROT = 410;
//-------------------------------------------------------------------------------------------------------------

struct Instruction {
    char instr;
    int page;
    Instruction(char i, int p) : instr(i), page(p) {}
};

struct VMA {
    int start_vpage;
    int end_vpage;
    bool write_protected;
    bool file_mapped;
    VMA(int start, int end, bool wp, bool fm) : 
        start_vpage(start), end_vpage(end), write_protected(wp), file_mapped(fm) {}
};

struct PTE {
    unsigned present:1;
    unsigned write_protect:1;
    unsigned modified:1;
    unsigned referenced:1;
    unsigned pagedout:1;
    unsigned frame:7;
    unsigned unused:20;
    PTE() : present(0), write_protect(0), modified(0), referenced(0), pagedout(0), frame(0), unused(0) {}
};

struct FTE {
    int pid;
    int vpage;
    unsigned int age;
    FTE() : pid(-1), vpage(-1), age(0) {} // -1 = frame is free
};

struct Process {
    int pid;
    vector<VMA> vmas;
    PTE page_table[PTE_ENTRIES];  
    unsigned long maps = 0, unmaps = 0, ins = 0, outs = 0, fins = 0, fouts = 0, zeros = 0, segv = 0, segprot = 0;
    Process(int id) : pid(id) {}
    void printProcessSummary() const {
        printf("PROC[%d]: U=%lu M=%lu I=%lu O=%lu FI=%lu FO=%lu Z=%lu SV=%lu SP=%lu\n",
            pid, unmaps, maps, ins, outs, fins, fouts, zeros, segv, segprot);
    }
};

vector<Process> processes;
vector<FTE> frame_table;

//-------------------------------------------- RANDOM VALUES -------------------------------------------------

vector<int> randvals;
int currentRandomIndex;

int get_random_number(int frame_count) {
    if (randvals.empty()) { cerr << "No random numbers available" << endl; exit(1); }
    if (currentRandomIndex >= randvals.size()) { currentRandomIndex = 0; }
    return randvals[currentRandomIndex++] % frame_count;
}

void read_random_file(const std::string& filename) {
    ifstream file(filename);
    int num;
    file >> num;  
    while (file >> num) { randvals.push_back(num); }
}

//----------------------------------------------- PARSING --------------------------------------------------------

struct Config {
    int num_frames = 0;     // f 
    char algo = '\0';       // a 
    bool O_option = false; 
    bool P_option = false;  
    bool F_option = false; 
    bool S_option = false;  
    string input_file;      
    string rand_file;
};

// command line arguments
Config parse_commands(int argc, char* argv[]) {
    Config config;
    int c;
    while ((c = getopt(argc, argv, "f:a:o:")) != -1) {
        switch (c) {
            case 'f':
                config.num_frames = stoi(optarg);
                if (config.num_frames <= 0 || config.num_frames > 128) {
                    cerr << "Invalid number of frames. Must be between 1 and 128" << endl;  exit(1);
                } break;
            case 'a':
                config.algo = optarg[0];
                if (string("frcewa").find(config.algo) == string::npos) {
                    cerr << "Invalid algorithm selection" << endl;  exit(1);
                } break;
            case 'o':
                for (char opt : string(optarg)) {
                    switch (opt) {
                        case 'O': config.O_option = true; break;
                        case 'P': config.P_option = true; break;
                        case 'F': config.F_option = true; break;
                        case 'S': config.S_option = true; break;
                        default:  cerr << "Invalid option: " << opt << endl; exit(1);
                    }
                } break;
            default:
                cerr << "Usage: " << argv[0] << " -f<num_frames> -a<algo> [-o<options>] inputfile randfile" << endl; exit(1);
        }
    }
    
    if (optind + 2 > argc) { cerr << "Missing input or random file" << endl; exit(1); }
    
    config.input_file = argv[optind];
    config.rand_file = argv[optind + 1];
    return config;
}

void parse_input_file(const string& filename) {
    ifstream infile(filename);
    if (!infile.is_open()) { cerr << "Cannot open input file: " << filename << endl; exit(1); }

    string line;
    int num_processes = 0;

    // no. of processes
    while (getline(infile, line)) {
        if (line[0] != '#') {
            num_processes = stoi(line);
            break;
        }
    }
    //process specs
    for (int i = 0; i < num_processes; i++) {
        Process proc(i);
        int num_vmas = 0;
        while (getline(infile, line)) {
            if (line[0] != '#') {  num_vmas = stoi(line); break;  }
        }
        for (int j = 0; j < num_vmas; j++) {
            while (getline(infile, line)) {
                if (line[0] != '#') {
                    int start, end, wp, fm;
                    istringstream iss(line);
                    iss >> start >> end >> wp >> fm;
                    proc.vmas.emplace_back(start, end, wp == 1, fm == 1);
                    break;
                }
            }
        }
        processes.push_back(proc);
    }
    
    infile.close(); // close file, reopen for reading instrs
}


class InstructionReader {
    private:
        ifstream infile;
        string line;
        bool instruction_section_started = false;

    public:
        InstructionReader(const string& filename) {
            infile.open(filename);
            if (!infile.is_open()) {
                cerr << "Cannot open input file: " << filename << endl;
                exit(1);
            }
        }

        bool get_next_instruction(char& operation, int& vpage) {
            while (getline(infile, line)) {
                if (!instruction_section_started) {
                    if (line == "#### instruction simulation ######") { instruction_section_started = true; }  continue; 
                }
                if (line.empty() || line[0] == '#') {  continue; }

                istringstream iss(line);
                iss >> operation >> vpage;
    
                if (operation == 'c' || operation == 'r' || operation == 'w' || operation == 'e') { return true; } 
                else { cerr << "Error: Invalid instruction type '" << operation << "' in line: " << line << endl; exit(1); }
            }
            return false;
        }

        ~InstructionReader() {
            if (infile.is_open()) {
                infile.close();
            }
        }
};


//------------------------------------------ PRINT FUNCTIONS -----------------------------------------------


void print_page_table(const Process& proc) {
    printf("PT[%d]: ", proc.pid);
    for (int i = 0; i < PTE_ENTRIES; i++) {
        const PTE& pte = proc.page_table[i];
        if (pte.present) {
            printf("%d:", i);
            printf("%c", pte.referenced ? 'R' : '-');
            printf("%c", pte.modified ? 'M' : '-');
            printf("%c", pte.pagedout ? 'S' : '-');
        } else { printf("%c", pte.pagedout ? '#' : '*'); }
        if (i < PTE_ENTRIES - 1) { printf(" ");}
    }
    printf("\n");
}


void print_frame_table() {
    printf("FT:");
    for (int i = 0; i < frame_table.size(); i++) {  
        if (frame_table[i].pid != -1) {
            printf(" %d:%d", frame_table[i].pid, frame_table[i].vpage);
        } else {
            printf(" *");  
        }
    }
    printf("\n");
}
// void print_frame_table() {
//     printf("FT:");
//     for (int i = 0; i < frame_table.size(); i++) { 
//         if (frame_table[i].pid != -1) {
//             printf(" %d:%d", frame_table[i].pid, frame_table[i].vpage);
//         }
//     }
//     printf("\n");
// }

//------------------------------------------ PAGER IMPLEMENTATIONS ---------------------------------------------------

class Pager {
    public:
        virtual ~Pager() = default;
        virtual FTE* select_victim_frame() = 0;
};

class FIFO : public Pager {
    int curr = 0;
    public:
        FTE* select_victim_frame() override {
            int victim = curr;
            curr = (curr + 1) % frame_table.size();
            return &frame_table[victim];
        }
};

class Random : public Pager {
    public:
        Random() {} 
        FTE* select_victim_frame() override {
            int frame_idx = get_random_number(frame_table.size()); //between 0 to num_frames-1
            return &frame_table[frame_idx];
        }
};

class Clock : public Pager {
    private: int hand;  //points to the oldest page
    public:
        Clock() : hand(0) {}
        FTE* select_victim_frame() override {
            int frames_checked = 0; 
            bool found = false;
            FTE* victim = nullptr;
            
            // until R=0 page found
            while (!found && frames_checked < frame_table.size()) {
                FTE& current = frame_table[hand];          //frame 
                Process& proc = processes[current.pid];    // frame ka process
                PTE& pte = proc.page_table[current.vpage];  // frame ke process ka pafe
                
                if (pte.referenced == 0) {
                    victim = &current;
                    found = true;
                } 
                else { pte.referenced = 0; } // give second chance -> clear R and move on
                hand = (hand + 1) % frame_table.size();
                frames_checked++;
            }
            
            // found no frame with ref=0 -> all pages get second chance
            if (!found) {
                victim = &frame_table[hand];
                hand = (hand + 1) % frame_table.size();
            }
            return victim;
        }
};

class NRU : public Pager {
    private:
        int curr;
        unsigned long last_reset;
        /*
            Class 0: (R=0,M=0) 
            Class 1: (R=0,M=1)
            Class 2: (R=1,M=0) 
            Class 3: (R=1,M=1)
        */
        int get_class(const FTE& frame) {
            if (frame.pid == -1) return -1;
            Process& proc = processes[frame.pid];
            PTE& pte = proc.page_table[frame.vpage];
            return (pte.referenced << 1) | pte.modified; //shift op: 1->10 0->00
        }
        
        // reset after 48+ instrs
        void reset_reference_bits() {
            for (int i = 0; i < frame_table.size(); i++) { 
                if (frame_table[i].pid != -1) {
                    Process& proc = processes[frame_table[i].pid];
                    proc.page_table[frame_table[i].vpage].referenced = 0;
                }
            }
            last_reset = instruction_counter;
        }

    public:
        NRU() : curr(0), last_reset(0) {}   
        FTE* select_victim_frame() override {
            bool do_reset = (instruction_counter - last_reset >= 48);
            vector<FTE*> class_frames[4]; // collect frames for each class
            int start_hand = curr;
            
            // classify all frames
            do {
                FTE& frame = frame_table[curr];
                if (frame.pid != -1) {
                    int class_num = get_class(frame);
                    if (class_num >= 0) { class_frames[class_num].push_back(&frame); }
                }
                curr = (curr + 1) % frame_table.size();
            } while (curr != start_hand);
            
            if (do_reset) { reset_reference_bits(); }
            
            // find lowest+non-empty class
            for (int class_num = 0; class_num < 4; class_num++) {
                if (!class_frames[class_num].empty()) {
                    FTE* victim = class_frames[class_num][0]; //  first frame 
                    curr = (victim - &frame_table[0] + 1) % frame_table.size();  
                    return victim;
                }
            }
            return &frame_table[0]; // this should never happen if there are frames in use
        }
};


class Aging : public Pager {
    private: int hand;
    public:
        Aging() : hand(0) {}
        void reset_age(int frame_num) { frame_table[frame_num].age = 0; }
        
        FTE* select_victim_frame() override {
            FTE* victim = nullptr;
            unsigned int lowest_weight = UINT_MAX;
            int start_hand = hand;

            // age all pages and find victim with lowest bit count
            do {
                FTE& current_frame = frame_table[hand];
                if (current_frame.pid != -1) {
                    Process& proc = processes[current_frame.pid];
                    PTE& pte = proc.page_table[current_frame.vpage];
                    
                    // age the page
                    current_frame.age = current_frame.age >> 1;
                    if (pte.referenced) {
                        current_frame.age = current_frame.age | 0x80000000;
                    }
                    pte.referenced = 0; // clear R after using it
                    
                    if (current_frame.age < lowest_weight) {
                        lowest_weight = current_frame.age;
                        victim = &current_frame;
                    }
                }
                hand = (hand + 1) % frame_table.size();
            } while (hand != start_hand);

            if (victim) { hand = ((victim - &frame_table[0]) + 1) % frame_table.size(); }    
            return victim ? victim : &frame_table[start_hand];
        }
};

class WorkingSet : public Pager {
    private:
        int hand;
        const unsigned int TAU = 49;
    public:
        WorkingSet() : hand(0) {}
        FTE* select_victim_frame() override {
            int start_hand = hand;
            FTE* oldest_frame = nullptr;
            unsigned int oldest_time = UINT_MAX;
            do {
                FTE& current = frame_table[hand];
                if (current.pid != -1) {
                    Process& proc = processes[current.pid];
                    PTE& pte = proc.page_table[current.vpage];
                    // victim = not referenced and outside window
                    if (!pte.referenced && (instruction_counter - current.age > TAU)) {
                        FTE* victim = &current;
                        hand = (hand + 1) % frame_table.size();
                        return victim;
                    }
                    if (pte.referenced) {
                        current.age = instruction_counter;
                        pte.referenced = 0;
                    }
                    // track oldest frame as fallback
                    if (current.age < oldest_time) {
                        oldest_time = current.age;
                        oldest_frame = &current;
                    }
                }
                hand = (hand + 1) % frame_table.size();
            } while (hand != start_hand);
            hand = (oldest_frame - &frame_table[0] + 1) % frame_table.size();
            return oldest_frame;
        }
};

//------------------------------------------- HELPER FUNCTIONS -------------------------------------------------------

const VMA* check_vma_access(const Process& proc, int vpage) {
    for (const auto& vma : proc.vmas) {
        if (vpage >= vma.start_vpage && vpage <= vma.end_vpage) { return &vma; }
    }
    return nullptr;
}

void return_frame_to_freelist(int frame_num) {
    free_frames.push_back(frame_num);
    frame_table[frame_num].pid = -1;
    frame_table[frame_num].vpage = -1;
}

/*
 handle_unmap:
    1. select victim frame 
    2. update page table of the removed frame's process
    3. OUT/FOUT
*/
void handle_unmap(Pager* pager, const Config& config, unsigned long long& cost) {
    FTE* victim_frame = pager->select_victim_frame();
    // if (!victim_frame) return;
    
    //get victim
    int frame_num = victim_frame - &frame_table[0];
    Process& old_proc = processes[victim_frame->pid];
    PTE& old_pte = old_proc.page_table[victim_frame->vpage];
    
    if (config.O_option) { printf(" UNMAP %d:%d\n", victim_frame->pid, victim_frame->vpage); }
    old_proc.unmaps++; cost += COST_UNMAP;
    
    // if page !modified, content in memory = disk : writing back would be unnecessary
    if (old_pte.modified) {
        const VMA* vma = check_vma_access(old_proc, victim_frame->vpage);
        if (vma && vma->file_mapped) {
            if (config.O_option) printf(" FOUT\n");
            old_proc.fouts++; cost += COST_FOUT;
        } else {
            if (config.O_option) printf(" OUT\n");
            old_proc.outs++; cost += COST_OUT;
            old_pte.pagedout = 1;  // this page has been paged out
        }
    }
    
    old_pte.present = 0;
    old_pte.referenced = 0;
    old_pte.modified = 0;
    
    return_frame_to_freelist(frame_num);
}

/*
    Allocate frame:
    1. check the free list.
    2. if no free frames 
        -> calls handle_unmap to free a frame using the replacement algo
        -> get a free frame now
*/
int allocate_frame(Pager* pager, const Config& config, unsigned long long& cost) {
    int frame_number;
    // chekc for free frame 
    if (!free_frames.empty()) {
        frame_number = free_frames.front();
        free_frames.pop_front();
        return frame_number;
    }
    // no free frames - replacement algorithm
    handle_unmap(pager, config, cost);  
    if (!free_frames.empty()) {
        frame_number = free_frames.front();
        free_frames.pop_front();
        return frame_number;
    }
    cerr << "Error: No frames available after page replacement" << endl; exit(1);
}

/*
    Handle page fault:
    check if page belongs to a valid VMA.
    allocates frame 
    update page table & frame table.
    initializes page: ZERO, IN, FIN
*/
void handle_page_fault(Process& proc, int vpage, const Config& config, Pager* pager, unsigned long long& cost) {
    const VMA* vma = check_vma_access(proc, vpage);
    if (!vma) {
        if (config.O_option) printf(" SEGV\n");
        proc.segv++;
        return;
    }
    
    int frame = allocate_frame(pager, config, cost);
    if (config.algo == 'a') {
        static_cast<Aging*>(pager)->reset_age(frame);
    }
    frame_table[frame].age = instruction_counter;

    // update pte
    PTE& pte = proc.page_table[vpage];
    pte.frame = frame;
    pte.present = 1;
    pte.write_protect = vma->write_protected;
    // update fte
    frame_table[frame].pid = proc.pid;
    frame_table[frame].vpage = vpage;
    
    if (pte.pagedout) { printf(" IN\n");  proc.ins++; } 
    else if (vma->file_mapped) { printf(" FIN\n"); proc.fins++; } 
    else { printf(" ZERO\n"); proc.zeros++; }
    printf(" MAP %d\n", frame);  proc.maps++;
}

//----------------------------------------------- SIMULATE -------------------------------------------------------

/*
    for each instruction:
            c: context switch 
            e: exit  (unmap all frames).
            r/w: read/write
                    check if page is present.
                    handle page fault
                    update metadata
        update simulation statistics
        output options
*/
void simulate(const Config& config, Pager* pager) {
    unsigned long long cost = 0;  // 64-bit 
    unsigned long ctx_switches = 0;
    unsigned long process_exits = 0;
    
    InstructionReader reader(config.input_file);
    char operation;
    int vpage;
    
    while (reader.get_next_instruction(operation, vpage)) {
        instruction_counter++;
       // cout<< "instr " << instruction_counter<<" : " << operation << " : " << vpage<<endl;
        if (config.O_option) { printf("%d: ==> %c %d\n", instruction_counter-1, operation, vpage); }
        
        switch(operation) {
            case 'c': { 
                current_process_number = vpage;
                ctx_switches++; cost += COST_CTX_SWITCH;  
                break;
            }
                
            case 'e': {  
                Process& proc = processes[current_process_number];
                if (config.O_option) {printf("EXIT current process %d\n", current_process_number);}
                for (int i = 0; i < PTE_ENTRIES; i++) {
                    PTE& pte = proc.page_table[i];
                    if (pte.present) {
                        if (config.O_option) { printf(" UNMAP %d:%d\n", current_process_number, i); }
                        proc.unmaps++; cost += COST_UNMAP;
                        if (pte.modified) {
                            const VMA* vma = check_vma_access(proc, i);
                            if (vma && vma->file_mapped) {
                                if (config.O_option) printf(" FOUT\n");
                                proc.fouts++; cost += COST_FOUT;
                            }
                        }  
                        return_frame_to_freelist(pte.frame);
                    }
                    pte = PTE(); 
                }
                process_exits++; cost += COST_PROC_EXIT;
                break;
            }
                
            case 'r':
            case 'w': {
                Process& proc = processes[current_process_number];
                PTE& pte = proc.page_table[vpage];
                cost += COST_READ_WRITE; 
                
                if (!pte.present) {
                    handle_page_fault(proc, vpage, config, pager, cost);
                    pte = proc.page_table[vpage];  // refresh pte
                    if (!pte.present){ 
                        cost += COST_SEGV;
                        continue; 
                    } 
                    cost += COST_MAP;
                    if (pte.pagedout) cost += COST_IN;
                    else if (check_vma_access(proc, vpage)->file_mapped) cost += COST_FIN;
                    else cost += COST_ZERO;
                }
                
                pte.referenced = 1;
                if (operation == 'w') {
                    if (pte.write_protect) {
                        if (config.O_option) printf(" SEGPROT\n");
                        proc.segprot++;
                        cost += COST_SEGPROT;  
                    } else {
                        pte.modified = 1;
                    }
                }
                break;
            }
        }
        
    }

    if (config.P_option) { for (const auto& proc : processes) {  print_page_table(proc);  }}
    if (config.F_option) { print_frame_table(); }
    if (config.S_option) {
        for (const auto& proc : processes) {  proc.printProcessSummary(); }
        printf("TOTALCOST %d %lu %lu %llu %lu\n", 
               instruction_counter, ctx_switches, process_exits, cost, sizeof(PTE));
    }
}

//-------------------------------------------------------------------------------------------------------------

void debug_print(const Config& config) {
    cout << "\n=== Command Line Arguments ===\n";
    cout << "Number of Frames: " << config.num_frames << endl;
    cout << "Algorithm: " << config.algo << endl;
    cout << "Options: ";
    
    if (config.O_option || config.P_option || config.F_option || config.S_option) {
        cout << "(";
        if (config.O_option) cout << "O";
        if (config.P_option) cout << "P";
        if (config.F_option) cout << "F";
        if (config.S_option) cout << "S";
        cout << ")";
    }
    cout << endl;
    cout << "Input file: " << config.input_file << endl;
    cout << "Random file: " << config.rand_file << endl;

    cout << "\n=== Processes and VMAs ===\n";
    for (const auto& proc : processes) {
        cout << "Process " << proc.pid << " has " << proc.vmas.size() << " VMAs:\n";
        for (const auto& vma : proc.vmas) {
            cout << "  VMA: " << vma.start_vpage << "-" << vma.end_vpage 
                 << " | write_protected: " << (vma.write_protected ? "yes" : "no")
                 << " | file_mapped: " << (vma.file_mapped ? "yes" : "no") << endl;
        }
        cout << endl;
    }
}

//-------------------------------------------------------------------------------------------------------------

void initialize_frame_table(int num_frames) {
    frame_table.resize(num_frames);
    for (int i = 0; i < num_frames; i++) {
        frame_table[i].pid = -1; frame_table[i].vpage = -1; frame_table[i].age = 0;
        free_frames.push_back(i);
    }
}

void setUp(const Config& config) {
    parse_input_file(config.input_file);
    read_random_file(config.rand_file);
    initialize_frame_table(config.num_frames);
    // debug_print(config);
}

int main(int argc, char **argv) {
    Config config = parse_commands(argc, argv);
    setUp(config);
    
    Pager* pager = nullptr;
    switch(config.algo) {
        case 'f': pager = new FIFO(); break;
        case 'r': pager = new Random(); break;
        case 'c': pager = new Clock(); break;
        case 'e': pager = new NRU(); break;
        case 'a': pager = new Aging(); break;
        case 'w': pager = new WorkingSet(); break;
    }
    
    simulate(config, pager);
    
    delete pager;
    return 0;
}
