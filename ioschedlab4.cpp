#include <unistd.h>
#include <fstream>
#include <iostream>
#include <list>
#include <sstream>
#include <string>
#include <vector>
#include <limits>
#include <algorithm>
#include <set>
#include <cmath>
#include <limits>
#include <deque>
#include <map>
#include <functional>
#include <queue>
#include <map>
#include <iostream>
#include <cstdlib>


using namespace std;

int current_track = 0;

bool vMode = false;
void log(const std::string& message) {
    if (vMode) {
        cout << "[LOG]: " << message << endl;
    }
}

//------------------------------------------------------------------------------------------------------------------------------

struct IORequest {
    int arrival_time;
    int track;
    int start_time = -1;  
    int finish_time = -1; 

    IORequest(int arrival_time, int track)
        : arrival_time(arrival_time), track(track) {}
};

class Scheduler {
public:
    list<int> ioQ;

    Scheduler() {}

    virtual ~Scheduler() {}

    virtual void add(int io_task_id) {
        log("base class `add` method called. IO index: " + to_string(io_task_id) + ".");
        ioQ.push_back(io_task_id);
    }

    virtual int get_next() {
        log("base class `get_next` method called.");
        if (ioQ.empty()) {
            log("base class: No IO requests available in the queue.");
            return -1;
        }
        int get_next_io = ioQ.front();
        log("base class selected IO request " + to_string(get_next_io) + ".");
        ioQ.pop_front();
        return get_next_io;
    }

    virtual bool is_free() {
        bool empty = ioQ.empty();
        log("base class `is_free` method called. Queue is " + string(empty ? "empty" : "not empty") + ".");
        return empty;
    }
};


Scheduler* sch = nullptr;
vector<IORequest> io_requests;
int processing_io = -1;
int simulation_time = 0;


class FIFOSched : public Scheduler {
public:
    FIFOSched() = default;
    ~FIFOSched() override = default;

    void add(int io_task_id) override { 
        log("adding IO request to the queue.");
        ioQ.push(io_task_id); 
    }

    int get_next() override {
        if (ioQ.empty()) {  return -1;  }

        int get_next_io = ioQ.front();
        log("selected IO request " + to_string(get_next_io) +
            " at track " + to_string(io_requests[get_next_io].track) + "."); 
        ioQ.pop();
        log("removed IO request  from the queue.");                
        return get_next_io;
    }

    bool is_free() override { 
        log("cecking if FIFOSched scheduler is empty.");
        return ioQ.empty(); 
    }

private:
    queue<int> ioQ; 
};


class SSTFSched : public Scheduler {
public:
    SSTFSched() = default;
    ~SSTFSched() override = default;

    void add(int io_task_id) override {
        log("adding IO request " + to_string(io_task_id) +
            " to the queue with track " + to_string(io_requests[io_task_id].track) + ".");
        ioQ.insert(io_task_id);
    } 

    int get_next() override {
        if (ioQ.empty()) {
            log("no IO requests available in the queue.");
            return -1;
        }

        auto closest_it = get_nearest_request();
        int chosen_request = *closest_it;
        log("selected IO request " + std::to_string(chosen_request) +
            " at track " + std::to_string(io_requests[chosen_request].track) + ".");

        remove_request(closest_it); 
        return chosen_request;
    }

    bool is_free() override {
        log("checking if SSTFSched scheduler is empty.");
        return ioQ.empty();
    }

private:
    std::set<int> ioQ;
    std::set<int>::iterator get_nearest_request() {
        return std::min_element(
            ioQ.begin(),
            ioQ.end(),
            [this](int a, int b) {
                return track_distance(a) < track_distance(b);
            }
        );
    }

    int track_distance(int io_task_id) const {
        const int track_position = io_requests[io_task_id].track;
        int distance = std::abs(track_position - current_track);
        log("measured the distance between the current position and the target.");
        return distance;
    }

    void remove_request(std::set<int>::iterator it) {
        if (it != ioQ.end()) {
            log("removing IO request " + std::to_string(*it) + " from the queue.");
            ioQ.erase(it);
        } else {
            log("attempted to remove an IO request, but it was not found in the queue.");
        }
    }
};


class LOOKSched : public Scheduler {
public:
    LOOKSched() : dir(1) {}
    ~LOOKSched() override = default;

    void add(int io_task_id) override {
        log("adding IO request " + std::to_string(io_task_id) + " to the queue.");
        ioQ.push_back(io_task_id);
    }

    int get_next() override {
        if (ioQ.empty()) { log("no IO requests available in the queue."); return -1; }

        int chosen_request = get_nearest_request();
        if (chosen_request == -1) {
            log("no valid requests in the current dir. Reversing dir.");
            reverse_dir(); 
            chosen_request = get_nearest_request();
        }

        if (chosen_request != -1) {
            log("selected IO request " + std::to_string(chosen_request) +
                " at track " + std::to_string(io_requests[chosen_request].track) + ".");
            auto it = std::find(ioQ.begin(), ioQ.end(), chosen_request); 
            if (it != ioQ.end()) {
                ioQ.erase(it); 
            }
        }
        return chosen_request;
    }

    bool is_free() override {
        log("checking if LOOK scheduler is empty.");
        return ioQ.empty();
    }

private:
    std::deque<int> ioQ; 
    int dir;          

 int get_nearest_request() const {
    log("finding the closest IO request in the current dir.");
    int chosen_request = -1;
    int min_distance = std::numeric_limits<int>::max();

    for (int io : ioQ) {
        int track = io_requests[io].track;
        bool is_valid_dir = (dir == 1 && track >= current_track) || 
                                  (dir == -1 && track <= current_track);

        if (is_valid_dir) {
            int distance = std::abs(track - current_track);
            if (distance < min_distance) {
                min_distance = distance;
                chosen_request = io;
            }
        }
    }
    if (chosen_request == -1) {
        log("no valid IO requests found in the current dir.");
    } else {
        log("closest IO request found at track " +
            std::to_string(io_requests[chosen_request].track) + ".");
    }
    return chosen_request;
}


void remove_request(int io_task_id) {
    auto it = std::find(ioQ.begin(), ioQ.end(), io_task_id);

    if (it != ioQ.end()) {
        ioQ.erase(it);
        log("io request " + std::to_string(io_task_id) + " removed successfully.");
    } 
    else { log("iO request " + std::to_string(io_task_id) + " not found in the queue."); }
}

void reverse_dir() {
    dir = -dir;
    log("dir reversed. New dir: " + string(dir == 1 ? "upward" : "downward") + ".");
}
};

class CLOOKSched : public Scheduler {
public:
    CLOOKSched() = default;
    ~CLOOKSched() override = default;

    void add(int io_task_id) override {
        log("adding IO request to the queue.");
        ioQ.push_back(io_task_id);
    }

int get_next() override {
    log("fetching the get_next IO request.");
    if (ioQ.empty()) { log("no IO requests available in the queue."); return -1;  }

    auto shortest_distance_it = find_closest_upward();
    if (shortest_distance_it == ioQ.end()) {
        log("no pending requests ahead. Wrapping around to the lowest track.");
        shortest_distance_it = find_closest_wraparound();
    }

    int chosen_request = *shortest_distance_it;
    ioQ.erase(shortest_distance_it); 
    return chosen_request;
}

    bool is_free() override {
        log("checking if CLOOKSched scheduler is empty.");
        return ioQ.empty();
    }

private:
    std::list<int> ioQ; 
    std::list<int>::iterator find_closest_upward() {
        log("finding the closest IO request in the upward dir.");
        auto closest_it = ioQ.end();
        int min_distance = std::numeric_limits<int>::max();

        for (auto it = ioQ.begin(); it != ioQ.end(); ++it) {
            int track = io_requests[*it].track;
            if (track >= current_track) { 
                int distance = track - current_track;
                if (distance < min_distance) {
                    min_distance = distance;
                    closest_it = it;
                }
            }
        }

        if (closest_it == ioQ.end()) {
            log("no valid upward requests found.");
        } else {
            log("closest upward IO request found at track " + 
                std::to_string(io_requests[*closest_it].track) + ".");
        }
        return closest_it;
    }

std::list<int>::iterator find_closest_wraparound() {
    log("finding the closest IO request by wrapping around to the lowest track.");
    return std::min_element(
        ioQ.begin(),
        ioQ.end(),
        [](int a, int b) {
            return io_requests[a].track < io_requests[b].track;
        }
    );
}
};


class FLOOKSched : public Scheduler {
public:
    FLOOKSched() : dir(1) {} 
    ~FLOOKSched() override = default;

    void add(int io_task_id) override {
        log("sdding IO request " + to_string(io_task_id) + " to the add queue.");
        add_queue.push_back(io_task_id);
    }

int get_next() override {
    if (active_queue.empty() && !add_queue.empty()) {
        log("active queue is empty. Swapping add queue with active queue.");
        swap_queues();
    }
    if (active_queue.empty()) {
        log("no IO requests available in the active queue.");
        return -1;
    }
    auto shortest_distance_it = get_nearest_request();
    if (shortest_distance_it == active_queue.end()) {
        reverse_dir();
        return get_next();
    }

    int chosen_request = *shortest_distance_it;
    log("selected IO request " + to_string(chosen_request) + 
            " at track " + to_string(io_requests[chosen_request].track) + ".");
    active_queue.erase(shortest_distance_it);
    return chosen_request;
}

    bool is_free() override {
        log("checking if FLOOKSched scheduler is empty.");
        return active_queue.empty() && add_queue.empty();
    }

private:
    list<int> active_queue;
    list<int> add_queue;
    int dir; 

    void swap_queues() {
        log("swapping add queue with active queue and resetting dir to upward.");
        active_queue.swap(add_queue);
        dir = 1; 
    }

    void reverse_dir() {
        dir = -dir;
    }

list<int>::iterator get_nearest_request() {
    log("finding the closest IO request in the current dir.");
    
    if (active_queue.empty()) {
        log("active queue is empty. No requests to process.");
        return active_queue.end();
    }

    auto compute_closest_request = [&](list<int>::iterator &current_closest, 
                                       int &min_distance, 
                                       list<int>::iterator &it) {
        int track = io_requests[*it].track;
        if (is_valid_track(track)) {
            int distance = abs(track - current_track); 
            if (distance < min_distance) {
                min_distance = distance;
                current_closest = it;
            }
        }
    };

    auto closest_it = active_queue.end();
    int min_distance = numeric_limits<int>::max();

    for (auto it = active_queue.begin(); it != active_queue.end(); ++it) {
        compute_closest_request(closest_it, min_distance, it);
    }

    if (closest_it == active_queue.end()) {
        log("no valid requests found in the current dir.");
    }
    return closest_it;
}

bool is_valid_track(int track) const {
    return (dir > 0 && track >= current_track) || 
           (dir < 0 && track <= current_track);
}

};

//------------------------------------------------------------------------------------------------------------------------------

bool is_valid_line(const std::string& line) {
    bool valid = !line.empty() && line[0] != '#';
    if (!valid) { log("ignoring invalid or comment line: " + line); }
    return valid;
}

void parse_and_add_io(const std::string& line) {
    std::istringstream iss(line);
    int arrival_time = 0, track = 0;
    if (iss >> arrival_time >> track) {
        io_requests.emplace_back(arrival_time, track);
        log("parsed and added IO request: Arrival Time = " + std::to_string(arrival_time) +
            ", Track = " + std::to_string(track) + ".");
    } else {
        log("failed to parse line: " + line);
    }
}

void read_input_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return;
    }

    log("started reading input file: " + filename);
    int line_count = 0; // counter for valid lines processed

    std::string line;
    while (std::getline(file, line)) { 
        //trim(line);

        if (line.empty() || line[0] == '#') {
            log("skipping comment or empty line.");
            continue;
        }

        if (is_valid_line(line)) {
            parse_and_add_io(line);
            ++line_count;
        } else {
            log("invalid line skipped: " + line);
        }
    }
    file.close();
}

void update_statistics(const IORequest& io, int& total_head_movement, double& total_turnaround_time, double& total_wait_time, int& longest_wait_time) {
    int wait_time = io.start_time - io.arrival_time;
    int head_movement = io.finish_time - io.start_time;
    int process_duration = io.finish_time - io.arrival_time;
    
    log("calculated statistics for IO request: wait_time = " + std::to_string(wait_time) +
        ", head_movement = " + std::to_string(head_movement) +
        ", process_duration = " + std::to_string(process_duration) + ".");

    total_head_movement += head_movement;
    total_turnaround_time += static_cast<double>(process_duration);
    total_wait_time += static_cast<double>(wait_time);
    longest_wait_time = std::max(longest_wait_time, wait_time);
    log("updated statistics for IO request: movement = " + std::to_string(head_movement) +
        ", turnaround time = " + std::to_string(process_duration) +
        ", wait time = " + std::to_string(wait_time) + ".");
}


void print_io_request(size_t index, const IORequest& io) {
    log("printing IO request details: Index = " + std::to_string(index) +
        ", Arrival = " + std::to_string(io.arrival_time) +
        ", Start = " + std::to_string(io.start_time) +
        ", Completion = " + std::to_string(io.finish_time) + ".");
    std::printf("%5zu: %5d %5d %5d\n", index, io.arrival_time, io.start_time, io.finish_time);
}


void print_io_details(int& total_head_movement, double& total_turnaround_time, double& total_wait_time, int& longest_wait_time) {
    log("printing details of all IO requests.");
    for (size_t i = 0; i < io_requests.size(); ++i) {
        const IORequest& io = io_requests.at(i);
        print_io_request(i, io); 
        update_statistics(io, total_head_movement, total_turnaround_time, total_wait_time, longest_wait_time);
    }
}

void print_summary_stats(int total_head_movement, double total_turnaround_time, double total_wait_time, int longest_wait_time) {
    log("calculating and printing summary statistics.");
    double num_requests = static_cast<double>(io_requests.size());
    double io_utilization = static_cast<double>(total_head_movement) / static_cast<double>(simulation_time);
    double avg_turnaround_time = total_turnaround_time / num_requests;
    double avg_wait_time = total_wait_time / num_requests;

    std::printf("SUM: %d %d %.4f %.2f %.2f %d\n", simulation_time, total_head_movement,
                io_utilization, avg_turnaround_time, avg_wait_time, longest_wait_time);

    log("summary statistics: Total Movement = " + std::to_string(total_head_movement) +
        ", IO Utilization = " + std::to_string(io_utilization) +
        ", Average Turnaround Time = " + std::to_string(avg_turnaround_time) +
        ", Average Wait Time = " + std::to_string(avg_wait_time) +
        ", Max Wait Time = " + std::to_string(longest_wait_time) + ".");
}

void print_summary() {
    int total_head_movement = 0;
    int longest_wait_time = 0;
    double total_turnaround_time = 0.0;
    double total_wait_time = 0.0;

    log("printing details of each IO operation.");
    print_io_details(total_head_movement, total_turnaround_time, total_wait_time, longest_wait_time); 

    log("Cclculating and printing summary statistics.");
    print_summary_stats(total_head_movement, total_turnaround_time, total_wait_time, longest_wait_time); 
}



void add_new_io_requests(int& io_ptr) {
    log("adding new IO requests to the scheduler at simulation time " + std::to_string(simulation_time) + ".");


    while (io_ptr < io_requests.size()) {
        const IORequest& io = io_requests[io_ptr];

        if (io.arrival_time > simulation_time) {
            log("no more IO requests to add. get_next request arrives at time " + std::to_string(io.arrival_time) + ".");
            break; 
        }

        if (io.arrival_time == simulation_time) {
            log("adding IO request " + std::to_string(io_ptr) + " with track " + std::to_string(io.track) + ".");
            sch->add(io_ptr);
            io_ptr++;
        }
    }
}

void complete_processing_io() {
    if (processing_io == -1) {
        log("no active IO request to complete.");
        return; 
    }

    const IORequest& current_io = io_requests[processing_io];
    if (current_io.track == current_track) {
        io_requests[processing_io].finish_time = simulation_time;
        log("completed IO request " + std::to_string(processing_io) +
            " at track " + std::to_string(current_io.track) +
            " at time " + std::to_string(simulation_time) + ".");
        processing_io = -1; 
    }
}


void complete_io_request(int io_task_id) {
    io_requests[io_task_id].finish_time = simulation_time;
    log("IO request " + std::to_string(io_task_id) + " completed at time " +
        std::to_string(simulation_time) + ".");
    processing_io = -1; 
}

void start_io_request(int get_next_io) {
    io_requests[get_next_io].start_time = simulation_time;
    processing_io = get_next_io;



    if (io_requests[get_next_io].track == current_track) {
        log("IO request " + std::to_string(get_next_io) +
            " is already at track head. Completing immediately.");
        complete_io_request(get_next_io);
    }
}

void process_get_next_io(int& io_ptr) {
    while (processing_io == -1) {
        int get_next_io = sch->get_next();

        if (get_next_io == -1) {
            if (io_ptr >= io_requests.size()) {
                log("no more IO requests to process.");
                return; 
            }
            log("no IO requests available in the scheduler.");
            break; 
        }
        log("processing get_next IO request " + std::to_string(get_next_io) + ".");
        start_io_request(get_next_io);
    }
}

//------------------------------------------------------------------------------------------------------------------------------


void simulation() {
    log("Starting simulation.");
    int io_ptr = 0;  

    while (true) {
        add_new_io_requests(io_ptr);  
        complete_processing_io();          
        process_get_next_io(io_ptr);      

        if (io_ptr >= io_requests.size() && processing_io == -1) {
            log("All IO requests processed. Ending simulation.");
            break; 
        }

        if (processing_io >= 0) {
            int movement;
            if (io_requests[processing_io].track > current_track) {
                movement = 1;
            } else {
                movement = -1;
            }
            
            log("Moving track head from " + std::to_string(current_track) + 
                " to " + std::to_string(current_track + movement) +
                " to reach track " + std::to_string(io_requests[processing_io].track));
            
            current_track += movement;
        }

        simulation_time++;              
    }
}

//------------------------------------------------------------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    char alg = '\0';
    std::string inputfile;

    log("Disk Scheduler simulation started.");

    int opt;
    while ((opt = getopt(argc, argv, "s:vqf")) != -1) {
        switch (opt) {
            case 's': // scheduler 
                if (optarg != nullptr) {
                    alg = optarg[0];
                    log("Scheduler algorithm set to: " + string(1, alg));
                } 
                else { cerr << "Error: Missing argument for -s option." << endl; return 1;}
                break;
            case 'v': // verbose 
                vMode = true;
                log("Verbose mode enabled.");
                break;
            case 'q': // queue debug
                log("Queue debug mode enabled.");
                break;
            case 'f': //  FLOOK debug
                log("FLOOK debug mode enabled.");
                break;
            default:
                cerr << "Error: Unknown option specified." << endl;
                return 1;
        }
    }

    if (optind < argc) { inputfile = argv[optind]; } 
    else { std::cerr << "Error: No input file specified." << std::endl; return 1;}

    log(std::string("Initializing scheduler with algorithm: ") + alg);
    switch (alg) {
        case 'N':
            sch = new FIFOSched();
            break;
        case 'S':
            sch = new SSTFSched();
            break;
        case 'L':
            sch = new LOOKSched();
            break;
        case 'C':
            sch = new CLOOKSched();
            break;
        case 'F':
            sch = new FLOOKSched();
            break;
        default:
            std::cerr << "Error: Invalid scheduler algorithm specified." << std::endl;
            return 1;
    }

    // simulation
    read_input_file(inputfile); 
    simulation();               
    print_summary();            

    // clean up
    delete sch;
    return 0;
}

