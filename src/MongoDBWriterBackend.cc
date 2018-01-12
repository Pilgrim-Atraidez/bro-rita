#include "MongoDBWriterBackend.h"
#include "DocBuilder.h"
#include <mongocxx/exception/bulk_write_exception.hpp>
#include <sstream>
#include <iomanip>
#include "writers/BufferedMongoDBWriter.h"
#include "writers/RotatedBufferedMongoDBWriter.h"
#include "writers/DailyBufferedMongoDBWriter.h"

using bsoncxx::builder::stream::close_array;
using bsoncxx::builder::stream::close_document;
using bsoncxx::builder::stream::document;
using bsoncxx::builder::stream::finalize;
using bsoncxx::builder::stream::open_array;
using bsoncxx::builder::stream::open_document;
using bsoncxx::builder::stream::open_document;


using namespace logging;
using namespace threading;
using namespace plugin::OCMDev_MongoDBWriter;
using namespace threading;
using namespace formatter;

MongoDBWriterBackend::MongoDBWriterBackend(WriterFrontend *frontend) :
        WriterBackend(frontend),
        formatter(new formatter::Ascii(this, formatter::Ascii::SeparatorInfo())) {
}

MongoDBWriterBackend::~MongoDBWriterBackend() {
    delete this->formatter;
}

bool MongoDBWriterBackend::DoInit(const WriterInfo &info, int num_fields,
                         const Field *const *fields) {

    if (BifConst::LogMongo::debug) {
        std::cout << "[logging::writer::MongoDB]" << std::endl;
        std::cout << "  path=" << info.path << std::endl;
        std::cout << "  rotation_interval=" << info.rotation_interval << std::endl;
        std::cout << "  rotation_base=" << info.rotation_base << std::endl;

        for (const auto &i : info.config) {
            std::cout << "  config[" << i.first << "] = " << i.second << std::endl;
        }

        for (int i = 0; i < num_fields; i++) {
            const Field *field = fields[i];
            std::cout << "  field " << field->name << ": "
                      << type_name(field->type) << std::endl;
        }

        std::cout << std::endl;
    }

    std::string uriInfo = LookupParam(info, "uri");
    if (uriInfo.empty()) {
        return false;
    }
    mongocxx::uri uri(uriInfo);

    std::string selectedDB = LookupParam(info, "selectedDB");
    if (selectedDB.empty()) {
        return false;
    }

    std::string logCollection = info.path;


    mongocxx::instance& instance = mongocxx::instance::current();
    this->client = make_shared<const mongocxx::client>(uri);

    std::string rotate = LookupParam(info, "rotate");
    std::transform(rotate.begin(), rotate.end(), rotate.begin(), ::tolower);

    std::string splitOnDay = LookupParam(info, "splitOnDay");
    std::transform(splitOnDay.begin(), splitOnDay.end(), splitOnDay.begin(), ::tolower);

    if (rotate == "true" || rotate == "t") {
        this->writer = make_unique<RotatedBufferedMongoDBWriter>(client, selectedDB, logCollection);
    } else if (splitOnDay == "true" || splitOnDay == "t") {
        this->writer = make_unique<DailyBufferedMongoDBWriter>(client, selectedDB, logCollection);
    } else {
        this->writer = make_unique<BufferedMongoDBWriter>(client, selectedDB, logCollection);
    }

    return this->writer->Init();

}


std::string MongoDBWriterBackend::LookupParam(const WriterInfo &info, const std::string& name) const {
    auto it = info.config.find(name.c_str());
    if (it == info.config.end())
        return std::string();
    else
        return it->second;
}

bool MongoDBWriterBackend::DoWrite(int num_fields, const Field *const *fields, Value **vals) {
    auto builder = DocBuilder(this->formatter);

    for (int i = 0; i < num_fields; i++) {
        builder.addField(fields[i], vals[i]);
    }

    return this->writer->Write(builder.finalize());
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
bool MongoDBWriterBackend::DoSetBuf(bool enabled) {
    //ignore. Always buffer
    return true;
}

bool MongoDBWriterBackend::DoRotate(const char *rotated_path, double open, double close, bool terminating) {
     if (!(this->writer->Rotate() && FinishedRotation("/dev/null", Info().path, open, close, terminating))) {
        Error(Fmt("error rotating %s", Info().path));
        return false;
    }

    return true;
}

bool MongoDBWriterBackend::DoFlush(double network_time) {
    return this->writer->Flush();
}

bool MongoDBWriterBackend::DoFinish(double network_time) {
    return this->writer->Flush();
}

bool MongoDBWriterBackend::DoHeartbeat(double network_time, double current_time) {
    return true;
}
#pragma clang diagnostic pop

