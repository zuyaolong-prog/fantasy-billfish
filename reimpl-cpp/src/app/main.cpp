#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "core/api/types.hpp"
#include "core/datacenter/billfish_datacenter.hpp"

namespace {

void print_info(const bf::core::api::ResponseInfo& info) {
    std::cout << "code=" << info.code << ", message=" << info.message << "\n";
}

int run_init(const std::filesystem::path& libRoot) {
    bf::core::datacenter::BillfishDataCenter dc;
    const auto created = dc.createLibrary(libRoot.string(), "BillfishClone");
    print_info(created.info);
    if (!created.info.ok()) {
        return 1;
    }

    const auto opened = dc.openLibrary(libRoot.string());
    print_info(opened.info);
    if (!opened.info.ok()) {
        return 1;
    }

    return dc.closeLibrary().info.ok() ? 0 : 1;
}

int run_demo(const std::filesystem::path& workspace) {
    const auto sampleDir = workspace / "samples";
    std::filesystem::create_directories(sampleDir);
    {
        std::ofstream(sampleDir / "cat.png", std::ios::binary) << "cat";
        std::ofstream(sampleDir / "dog.png", std::ios::binary) << "dog";
    }

    const auto libRoot = workspace / "library";
    bf::core::datacenter::BillfishDataCenter dc;

    auto r = dc.createLibrary(libRoot.string(), "Demo");
    print_info(r.info);
    if (!r.info.ok()) {
        return 1;
    }

    auto opened = dc.openLibrary(libRoot.string());
    print_info(opened.info);
    if (!opened.info.ok()) {
        return 1;
    }

    bf::core::api::ImportRequest req;
    req.entries = {sampleDir.string()};
    req.mode = bf::core::api::ImportMode::ImportIndex;
    auto imported = dc.importFiles(req);
    print_info(imported.info);
    std::cout << "imported=" << imported.importedIds.size() << ", failed=" << imported.failed.size() << "\n";

    auto tag = dc.tagNew("pet");
    print_info(tag.info);
    if (tag.info.ok()) {
        dc.tagAttach(imported.importedIds, {tag.tagId});
    }

    bf::core::api::SearchSummaryRequest sreq;
    sreq.query = "pet";
    auto searched = dc.searchSummary(sreq);
    print_info(searched.info);
    std::cout << "search hits=" << searched.items.size() << "\n";

    bf::core::api::ExportRequest ereq;
    ereq.mode = "Folder";
    ereq.fileIds = imported.importedIds;
    ereq.destination = (workspace / "exports").string();
    auto exported = dc.exportFiles(ereq);
    print_info(exported.info);
    std::cout << "exported=" << exported.exported << "\n";

    dc.closeLibrary();
    return 0;
}

void print_usage() {
    std::cout << "Billfish Clone CLI\n"
              << "usage:\n"
              << "  bfcli init <library_path>\n"
              << "  bfcli demo <workspace_path>\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage();
        return 1;
    }

    const std::string cmd = argv[1];
    const std::filesystem::path arg = argv[2];

    if (cmd == "init") {
        return run_init(arg);
    }
    if (cmd == "demo") {
        return run_demo(arg);
    }

    print_usage();
    return 1;
}
