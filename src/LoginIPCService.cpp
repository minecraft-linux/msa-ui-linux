#include "LoginIPCService.h"
#include "WebLoginWindow.h"
#include "UIThreadExecutor.h"

LoginIPCService::LoginIPCService(Gtk::Application& app, UIThreadExecutor& executor, const std::string& path,
                                 ExtensionIPCServer& extension_ipc_server,
                                 daemon_utils::shutdown_policy shutdown_policy) :
        app(app), executor(executor), auto_shutdown_service(path, shutdown_policy),
        extension_ipc_server(extension_ipc_server) {
    using namespace std::placeholders;
    add_handler("msa/ui/exit", std::bind(&LoginIPCService::handle_exit, this));
    add_handler_async("msa/ui/open_browser", std::bind(&LoginIPCService::handle_open_browser, this, _3, _4));

    has_app_ref = true;
    app.hold();
}

void LoginIPCService::request_stop() {
    auto_shutdown_service::request_stop();
    if (has_app_ref) {
        app.release();
        has_app_ref = false;
    }
}

simpleipc::rpc_json_result LoginIPCService::handle_exit() {
    request_stop();
    return simpleipc::rpc_json_result::response(nullptr);
}

void LoginIPCService::handle_open_browser(nlohmann::json const& data, rpc_handler::result_handler const& handler) {
    std::string url = data["url"];

    executor.run([this, url, handler]() {
        if (extension_ipc_server.has_window()) {
            handler(simpleipc::rpc_json_result::error(-500, "A browser operation is already in progress"));
            return;
        }
        WebLoginWindow* login_window = new WebLoginWindow(url);
        login_window->signal_hide().connect([this, handler, login_window]() {
            if (login_window->has_succeeded()) {
                nlohmann::json res;
                auto& prop = res["properties"] = nlohmann::json::object();
                for (auto const& p : login_window->get_result())
                    prop[p.first] = p.second;
                handler(simpleipc::rpc_json_result::response(res));
            } else {
                handler(simpleipc::rpc_json_result::error(-501, "Operation cancelled by user"));
            }
            extension_ipc_server.set_window(nullptr);
            delete login_window;
        });
        extension_ipc_server.set_window(login_window);
        login_window->show();
    });
}