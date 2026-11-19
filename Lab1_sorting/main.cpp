#include <iostream>
#include <vector>
#include <queue>
#include <chrono>
#include <utility>
#include"utils.h"
#include<string>

SortResult parallelSortWithMerge(std::vector<int>& vec, int numThreads,bool isDebug);


enum ReturnCode{
    Successful,
    Help,
    Error,
};

struct Args{
    int n = 100;
    int threads = 1;
    bool isDebug = false;
    std::string outputFile;
    ReturnCode returnCode = ReturnCode::Successful;
    std::string error_message;
};

Args parse_args(int argc,char* argv[]){
    Args args;
    std::unordered_map<std::string,std::string> args_map;
    if(argc == 0){
        args.returnCode = ReturnCode::Help;
        return args; 
    }
    for(int i = 1; i< argc;i++){
        std::string arg = argv[i];
        if(arg == "--help" || arg == "-h"){
            args.returnCode = ReturnCode::Help;
            return args; 
        }
        if(arg == "-d" || arg == "--debug"){
            args.isDebug =true;
            continue;
        }
        try{
            auto splitted = split(arg,'=');
            if(splitted.size() !=2){
                args.returnCode = ReturnCode::Error;
                args.error_message = "Error! Parse error. Actual size = "+std::to_string(splitted.size());
                return args;
            }
            auto key = splitted[0];
            if(!(key == "--size" || key == "--threads" || key == "--output")){
                args.returnCode = ReturnCode::Error;
                args.error_message = "Error! Unable to find argument = "+key;
                return args;
            }
            auto val = splitted[1];
            if(args_map.contains(key)){
                args.returnCode = ReturnCode::Error;
                args.error_message = "Error! Duplicates key "+key;
                return args;
            }
            args_map[key]=val;

        }   
        catch(const std::exception& e){
            args.returnCode = ReturnCode::Error;
            std::cout<<"error";
            args.error_message = e.what();
            return args;
        }
    }
    try{
    if(args_map.contains("--size")){
        std::cout<<args_map["--size"]<<'\n';
        args.n = std::stoi(args_map["--size"]);
    }
    if(args_map.contains("--threads")){
        args.threads = std::stoi(args_map["--threads"]);
    }
    if(args_map.contains("--output")){
        args.outputFile = args_map["--output"];
    }
    }
    catch(std::invalid_argument ex){
        args.returnCode = ReturnCode::Error;
        args.error_message = "Error! Unable to parse argument.";
        return args;
    }
    return args;
}   


int main(int argc,char* argv[]) {

    Args args = parse_args(argc,argv);
    switch(args.returnCode){
        case ReturnCode::Successful:
            break;
        case ReturnCode::Help:
            std::cout<<"Help";
            return 0;
            break;
        case ReturnCode::Error:
            std::cout<<"Error: "<<args.error_message;
            return 1;
            break;
    }
    int n = args.n;
    int threadCount = args.threads;

    std::vector<int> data(n);
    data.reserve(n);

    for(int i =0;i<n;i++){
        data[i] = rand() % 1000;
    }
    if(args.isDebug){
        std::cout << "Original array:\n";
        Printer::print(data,n);
    }
    SortResult res = parallelSortWithMerge(data, threadCount,args.isDebug);

    if(args.isDebug){
        std::cout << "\nSorted array:\n";
        Printer::print(data,n);
    }
    std::cout<<"Threads:" <<res.thread_count<<'\n'<<"Size: "<<res.arraySize<<'\n'<<"Time: "<<res.ms<<" ms";

    return 0;
}