#pragma once

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

struct RunMetadata {
    std::filesystem::path executable;
    std::filesystem::path working_directory;
    std::filesystem::path model;
    std::filesystem::path fan_curves;
    std::filesystem::path case_directory;
    std::filesystem::path geometry;
    std::filesystem::path simulation;
    std::string backend;
    std::string mode;
};

inline std::string json_escape(const std::string& value) {
    std::ostringstream output;
    for(const unsigned char character : value) {
        switch(character) {
        case '\\': output << "\\\\"; break;
        case '"': output << "\\\""; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if(character < 0x20)
                output << "\\u" << std::hex << std::setw(4)
                       << std::setfill('0') << static_cast<int>(character)
                       << std::dec;
            else output << static_cast<char>(character);
        }
    }
    return output.str();
}

inline std::string utc_timestamp() {
    const auto now=std::chrono::system_clock::now();
    const std::time_t value=std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc,&value);
#else
    gmtime_r(&value,&utc);
#endif
    std::ostringstream output;
    output << std::put_time(&utc,"%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

inline void write_run_metadata(
    const std::filesystem::path& destination,
    const RunMetadata& metadata) {
    const auto absolute=[](const std::filesystem::path& path) {
        return path.empty() ? std::string() :
            std::filesystem::absolute(path).lexically_normal().string();
    };
    const std::filesystem::path temporary=destination.string()+".tmp";
    std::ofstream output(temporary,std::ios::trunc);
    if(!output)
        throw std::runtime_error(
            "Unable to create run metadata: "+temporary.string());
    const auto property=[&output](const char* name,const std::string& value,
                                  const bool comma=true) {
        output << "  \"" << name << "\": \"" << json_escape(value)
               << "\"" << (comma ? "," : "") << "\n";
    };
    output << "{\n  \"schema_version\": 1,\n";
    property("written_at_utc",utc_timestamp());
    property("executable",absolute(metadata.executable));
    property("working_directory",absolute(metadata.working_directory));
    property("model",absolute(metadata.model));
    property("fan_curves",absolute(metadata.fan_curves));
    property("backend",metadata.backend);
    property("mode",metadata.mode);
    property("case_directory",absolute(metadata.case_directory));
    property("geometry",absolute(metadata.geometry));
    property("simulation",absolute(metadata.simulation),false);
    output << "}\n";
    output.close();
    if(!output)
        throw std::runtime_error(
            "Unable to finish run metadata: "+temporary.string());
    std::error_code error;
    std::filesystem::remove(destination,error);
    error.clear();
    std::filesystem::rename(temporary,destination,error);
    if(error)
        throw std::runtime_error(
            "Unable to publish run metadata '"+destination.string()+
            "': "+error.message());
}
