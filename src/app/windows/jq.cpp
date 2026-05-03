#include "../app.h"

#include <cassert>
#include <chrono>
#include <future>
#include <filesystem>
#include <optional>
#include <utility>

#ifndef _MSC_VER
#include <jq.h>
#include <jv.h>


struct AppJSONExplorer::JQWrapperBase {
    std::string_view json;
    jv parsed;

    int opts = 0;

    using Result = std::optional<result_t>;

    Result output;
    std::future<Result> worker;

    JQWrapperBase(std::string_view, std::string_view json) : json(json) {
        parsed = jv_parse(json.data());
    }
    ~JQWrapperBase() {
        jv_free(parsed);
    }

    void set_compact(bool value) {
        if (value) opts = 0;
        else opts = JV_PRINT_INDENT_FLAGS(2);
    }

    bool update(std::string_view filter) {
        if (worker.valid()) {
            return false;
        }

        output = std::nullopt;
        worker = std::async(std::launch::async, [this](std::string_view filter) {
            return run(filter);
        }, filter);
        return true;
    }

    Result run(std::string_view filter) {
        Result output;
        auto& [cout, cerr] = output.emplace();
        const auto error_callback = [](void* data, jv msg) {
            msg = jq_format_error(msg);
            auto& cerr = * (std::string*) data;
            if (!cerr.empty()) cerr.push_back('\n');
            cerr.append(jv_string_value(msg));
            jv_free(msg);
        };

        jq_state* jq = jq_init();
        assert(jq);

        jq_set_error_cb(jq, error_callback, (void*) &cerr);

        if (jq_compile(jq, filter.data())) {
            jq_start(jq, jv_copy(parsed), 0);

            jv item;
            while (jv_is_valid(item = jq_next(jq))) {
                cout.append(jv_string_value(item = jv_dump_string(item, opts)));
                cout.push_back('\n');
                jv_free(item);
            }
        }

        jq_teardown(&jq);
        return output;
    }

    bool check(std::chrono::milliseconds wait_for) {
        if (!worker.valid()) return true;

        switch (worker.wait_for(wait_for)) {
            case std::future_status::deferred:
            case std::future_status::timeout:
                return false;
            case std::future_status::ready:
                output = worker.get();
                return true;
        }
        std::unreachable();
    }

    const Result& get_output() {
        check(std::chrono::milliseconds(2));
        return output;
    }
};
#else
#include <windows.h>


struct AppJSONExplorer::JQWrapperBase {
    std::filesystem::path jq_location;
    std::string_view json;

    bool compact = false;
    std::optional<result_t> output;
    std::future<result_t> worker;

    JQWrapperBase(const std::string_view here, std::string_view json) : json(json) {
        jq_location = here;
        jq_location = jq_location / JQ_PATH;
    }
    ~JQWrapperBase() {
    }

    bool update(std::string_view filter) {
        if (worker.valid()) {
            return false;
        }

        output = std::nullopt;
        worker = std::async(std::launch::async, [this](std::string_view filter) {
            return run(filter);
        }, filter);
        return true;
    }

    std::string escape_shell(auto&& ...args) {
        std::string cli = "";
        (([&cli](std::string_view arg) {
            if (arg.empty()) return;

            if (!cli.empty()) cli.push_back(' ');

            cli.push_back('"');
            for (const char c : arg) {
                if (c == '"') {
                    cli.push_back('\\');
                }
                cli.push_back(c);
            }
            cli.push_back('"');
        })(std::forward<decltype(args)>(args)), ...);
        return cli;
    }

    result_t run(std::string_view filter) {
        HANDLE g_hChildStd_IN_Rd = NULL;
        HANDLE g_hChildStd_IN_Wr = NULL;
        HANDLE g_hChildStd_OUT_Rd = NULL;
        HANDLE g_hChildStd_OUT_Wr = NULL;
        
        try {
            SECURITY_ATTRIBUTES saAttr;
            saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
            saAttr.bInheritHandle = TRUE;
            saAttr.lpSecurityDescriptor = NULL;

            if (!CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, 0)) {
                throw std::runtime_error("stdout createpipe failed");
            }
            if (!SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0)) {
                throw std::runtime_error("stdoutrd sethandleinformation failed");
            }
            if (!CreatePipe(&g_hChildStd_IN_Rd, &g_hChildStd_IN_Wr, &saAttr, 0)) {
                throw std::runtime_error("stdin createpipe failed");
            }
            if (!SetHandleInformation(g_hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0)) {
                throw std::runtime_error("stdinwr sethandleinformation failed");
            }

            PROCESS_INFORMATION piProcInfo;
            STARTUPINFO siStartInfo;
            BOOL bSuccess = FALSE;

            ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
            ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));

            siStartInfo.cb = sizeof(STARTUPINFO);
            siStartInfo.hStdError = g_hChildStd_OUT_Wr;
            siStartInfo.hStdOutput = g_hChildStd_OUT_Wr;
            siStartInfo.hStdInput = g_hChildStd_IN_Rd;
            siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

            std::string cli = escape_shell(jq_location.string(), filter, compact ? "-Mc" : "-M");
            bSuccess = CreateProcess(NULL, cli.data(), NULL, NULL, TRUE, 0, NULL, NULL,
                                     &siStartInfo, &piProcInfo);

            if (!bSuccess) {
                throw std::runtime_error("failed to create process");
            }
            if (!CloseHandle(piProcInfo.hProcess)) {
                throw std::runtime_error("failed to close process handle");
            }
            if (!CloseHandle(piProcInfo.hThread)) {
                throw std::runtime_error("failed to close process thread handle");
            }

            if (!CloseHandle(g_hChildStd_OUT_Wr)) {
                throw std::runtime_error("failed to close stdoutwr");
            }
            g_hChildStd_OUT_Wr = NULL;
            if (!CloseHandle(g_hChildStd_IN_Rd)) {
                throw std::runtime_error("failed to close stdinrd");
            }
            g_hChildStd_IN_Rd = NULL;

            constexpr static std::size_t bufsize = 1 << 16;
            
            bool failed = false;
            for (std::size_t i = 0; i < json.size(); i += bufsize) {
                const std::size_t diff = i + bufsize < json.size() ? bufsize : json.size() - i;
                if (!WriteFile(g_hChildStd_IN_Wr, json.data() + i, diff, NULL, NULL)) {
                    failed = true;
                    break;
                }
            }
            if (!CloseHandle(g_hChildStd_IN_Wr)) {
                throw std::runtime_error("failed to close stdinwr");
            }
            g_hChildStd_IN_Wr = NULL;
            
            using block_t = std::pair<DWORD, std::unique_ptr<char[]>>;
            std::vector<block_t> blocks;
            std::size_t size = 0;
            while (true) {
                auto& [read, ptr] = blocks.emplace_back(0, std::make_unique<char[]>(bufsize));
                if (!ReadFile(g_hChildStd_OUT_Rd, ptr.get(), bufsize, &read, NULL) || !read) {
                    blocks.pop_back(); 
                    break;
                }
                size += read;
            }
            std::string value;
            value.reserve(size);
            for (const auto& [cnt, buf] : blocks) {
                value.append(buf.get(), buf.get() + cnt);
            }
            if (failed) {
                return { "", value };
            } else {
                return { value, "" };
            }
        } catch (const std::runtime_error& e) {
            std::string msg = e.what();

            if (g_hChildStd_IN_Rd != NULL &&!CloseHandle(g_hChildStd_IN_Rd)) {
                return { "", msg + " + failed to close handle: stdinrd" };
            }
            if (g_hChildStd_IN_Wr != NULL &&!CloseHandle(g_hChildStd_IN_Wr)) {
                return { "", msg + " + failed to close handle: stdinwr" };
            }
            if (g_hChildStd_OUT_Rd != NULL &&!CloseHandle(g_hChildStd_OUT_Rd)) {
                return { "", msg + " + failed to close handle: stdoutrd" };
            }
            if (g_hChildStd_OUT_Wr != NULL &&!CloseHandle(g_hChildStd_OUT_Wr)) {
                return { "", msg + " + failed to close handle: stdoutwr" };
            }
            return { "", msg };
        }
    }

    void set_compact(bool value) {
        compact = value;
    }

    const std::optional<result_t>& get_output() {
        if (worker.valid()) {
            switch (worker.wait_for(std::chrono::milliseconds(2))) {
                case std::future_status::deferred:
                case std::future_status::timeout:
                    break;
                case std::future_status::ready:
                    output = worker.get();
            }
        }
        return output;
    }
};
#endif


AppJSONExplorer::JQWrapper::JQWrapper(std::string_view here, std::string_view json) {
    base = std::make_unique<JQWrapperBase>(here, json);
}
AppJSONExplorer::JQWrapper::~JQWrapper() {}
AppJSONExplorer::JQWrapper::JQWrapper(JQWrapper&&) = default;

bool AppJSONExplorer::JQWrapper::update(std::string_view filter) {
    return base->update(filter);
}
void AppJSONExplorer::JQWrapper::set_compact(bool value) {
    return base->set_compact(value);
}
const std::optional<AppJSONExplorer::result_t>& AppJSONExplorer::JQWrapper::get_output() {
    return base->get_output();
}
