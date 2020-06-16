#include <iostream>
#include <xfinal.hpp>
using namespace xfinal;

std::vector<std::string> pass_word_pool;
std::vector<std::shared_ptr<websocket>> wss_;
class wsBroadCast {
//public:
//	wsBroadCast() {
//		post_msg_thread_ = std::make_unique<std::thread>([this]() {
//			while (true) {
//				std::unique_lock<std::mutex> lock(mutex_);
//				cdvr_.wait(lock, [this]() {
//					return msg_queue_.empty() == false;
//				});
//				auto msg_pair = std::move(msg_queue_.front());
//				msg_queue_.pop();
//				lock.unlock();
//				std::unique_lock<std::mutex> lock_list(addrm_mutex_);
//				auto copy = wslist_;
//				lock_list.unlock();
//				for (auto& iter : copy) {
//					if (iter.first != msg_pair.first) {
//						iter.second->write_string(msg_pair.second);
//					}
//				}
//			}
//		});
//		post_msg_thread_->detach();
//	}
public:
	void add(std::string const& token, std::string const& name, std::weak_ptr<websocket> weak) {
		std::unique_lock<std::mutex> lock(addrm_mutex_);
		auto ws = weak.lock();
		if (ws != nullptr) {
			wslist_[token] = ws;
			name_list_[token] = name;
		}
	}

	void remove_text(std::string const& token) {
		std::unique_lock<std::mutex> lock(addrm_mutex_);
		auto iter = wslist_.find(token);
		if (iter != wslist_.end()) {
			wslist_.erase(iter);
			name_list_.erase(token);
		}
	}

	void remove_img(std::string const& token) {
		std::unique_lock<std::mutex> lock2(img_mutex_);
		auto iter = wsimg_list_.find(token);
		if (iter != wsimg_list_.end()) {
			wsimg_list_.erase(iter);
			name_list_img_.erase(token);
		}
	}

	void broadcast(std::string const& except_token, std::string const& message) {
		std::unique_lock<std::mutex> lock(addrm_mutex_);
		auto copy = wslist_;
		lock.unlock();
		for (auto& iter : copy) {
			if (iter.first != except_token) {
				iter.second->write_string(message);
			}
		}
	}

	void addImg(std::string const& token, std::string const& name, std::weak_ptr<websocket> weak) {
		std::unique_lock<std::mutex> lock(img_mutex_);
		auto ws = weak.lock();
		if (ws != nullptr) {
			wsimg_list_[token] = ws;
			name_list_img_[token] = name;
		}
	}

	void broadcastImg(std::string const& except_token, std::string const& message) {
		std::unique_lock<std::mutex> lock(img_mutex_);
		auto copy = wsimg_list_;
		lock.unlock();
		for (auto& iter : copy) {
			if (iter.first != except_token) {
				iter.second->write_string(message);
			}
		}
	}

	std::string getname(std::string const& uuid) {
		std::unique_lock<std::mutex> lock(addrm_mutex_);
		auto iter = name_list_.find(uuid);
		if (iter != name_list_.end()) {
			return iter->second;
		}
		return "";
	}
	std::string getname_img(std::string const& uuid) {
		std::unique_lock<std::mutex> lock(img_mutex_);
		auto iter = name_list_img_.find(uuid);
		if (iter != name_list_img_.end()) {
			return iter->second;
		}
		return "";
	}
private:
	std::mutex img_mutex_;
	std::mutex addrm_mutex_;
	std::unordered_map<std::string, std::shared_ptr<websocket>> wslist_;
	std::unordered_map<std::string, std::shared_ptr<websocket>> wsimg_list_;
	std::unordered_map<std::string, std::string> name_list_;
	std::unordered_map<std::string, std::string> name_list_img_;
	//std::unique_ptr<std::thread> post_msg_thread_;
	//std::condition_variable cdvr_;
	//std::queue<std::pair<std::string,std::string>> msg_queue_;
};

struct SesionInterceptor {
	bool prehandle(request& req, response& res) {
		auto& session = req.session();
		if (session.get_data<bool>("validate") != true) {
			res.write_state(http_status::bad_request);
			return false;
		}
		return true;
	}
	bool posthandle(request& req, response& res) {
		return true;
	}
};
int main()
{
	std::ifstream file("./config.json");
	std::stringstream ss;
	ss << file.rdbuf();
	auto config_json = json::parse(ss.str());
	auto thread_size = std::thread::hardware_concurrency();
	http_server server(thread_size);
	server.listen("0.0.0.0", "8020");

	server.set_websocket_check_read_alive_time(60);
	server.set_websocket_check_write_alive_time(60);

	server.on_error([](std::string const& err) {
		std::cout << err << std::endl;
	});

	wsBroadCast broadcast_;

	server.router<GET>("/addpass", [](request& req, response& res) {
		bool existed = false;
		auto pass = req.param("pass");
		if (pass.empty()) {
			res.write_string("fail", false, xfinal::http_status::bad_request);
			return;
		}
		for (auto& iter : pass_word_pool) {
			if (iter == view2str(pass)) {
				existed = true;
				break;
			}
		}
		if (!existed) {
			pass_word_pool.push_back(view2str(pass));
			res.write_string("OK");
		}
		else {
			res.write_string("existed");
		}
	});

	server.router<GET>("/rmpass", [](request& req, response& res) {
		auto pass = req.param("pass");
		if (pass.empty()) {
			res.write_string("fail", false, xfinal::http_status::bad_request);
			return;
		}
		for (auto iter = pass_word_pool.begin(); iter != pass_word_pool.end();) {
			if (*iter == view2str(pass)) {
				pass_word_pool.erase(iter);
				break;
			}
			iter++;
		}
		});

	server.router<GET>("/", [config_json](request& req, response& res) {
		res.set_attr("base_path", config_json["path"].get<std::string>());
		res.set_attr("host", config_json["url"].get<std::string>());
		res.write_file_view("./www/index.html", true);
	});


	server.router<POST>("/login", [config_json](request& req, response& res) {
		auto real_name = req.query("real_name");
		auto pass_word = req.query("password");
		if (real_name.empty() || pass_word.empty()) {
			res.redirect("http://" + config_json["url"].get<std::string>() + "/");
			return;
		}
		bool existed = false;
		//auto size = pass_word_pool.size();
		for (auto& iter : pass_word_pool) {
			if (iter == view2str(pass_word)) {
				existed = true;
				break;
			}
		}
		if (existed) {
			auto& session = req.create_session();
			session.set_data("real_name", view2str(real_name));
			session.set_data("validate", true);
			session.set_expires(3600);
			session.get_cookie().set_path(config_json["path"].get<std::string>());
			res.redirect("http://" + config_json["url"].get<std::string>() + "/chat");
		}
		else {
			res.redirect("http://" + config_json["url"].get<std::string>() + "/");
		}
	});




	server.router<GET>("/chat", [config_json](request& req, response& res) {
		auto& session = req.session();
		if (session.get_data<bool>("validate") == true) {
			auto real_name = session.get_data<std::string>("real_name");
			res.set_attr("real_name", real_name);
			res.set_attr("tokenid", session.get_id());
			res.set_attr("host", config_json["url"].get<std::string>());
			res.write_file_view("./www/chat.html");
		}
		else {
			res.redirect("http://" + config_json["url"].get<std::string>() + "/");
		}
	});

	server.router<POST>("/upload", [&broadcast_](request& req, response& res) {
		auto& file = req.file("file");
		auto tokenId = view2str(req.query("tokenid"));
		json root;
		root["success"] = true;
		root["type"] = "chat";
		root["ctype"] = "file";
		root["src"] = file.url_path();
		root["size"] = file.size();
		root["fname"] = file.original_name();
		auto& session = req.session();
		auto real_name = session.get_data<std::string>("real_name");
		root["name"] = real_name;
		broadcast_.broadcast(tokenId, root.dump());
		res.write_json(root);
		}, SesionInterceptor{});



	websocket_event event;
	event.on("message", [&broadcast_](websocket& ws) {
		auto msg_str = view2str(ws.messages());
		try {
			auto msg = json::parse(msg_str);
			auto type = msg["type"].get<std::string>();
			if (type == "add") {
				json root;
				root["type"] = "tip";
				root["message"] = msg["name"].get<std::string>() + " 加入聊天";
				auto tokenid = msg["tokenid"].get<std::string>();
				ws.set_user_data("tokenid", std::shared_ptr<std::string>(new std::string(tokenid)));
				broadcast_.broadcast(tokenid, root.dump());
				broadcast_.add(tokenid,msg["name"].get<std::string>(), ws.shared_from_this());
			}
			else if (type == "chat") {
				auto tokenid = msg["tokenid"].get<std::string>();
				broadcast_.broadcast(tokenid, msg_str);
			}
		}
		catch (std::exception const& ec) {
			std::cout << "json parse error" << std::endl;
			auto tmpoint = std::time(nullptr);
			std::ofstream file("./" + std::to_string(tmpoint) + "_error.json", std::ios::app);
			file << msg_str;
			file.close();
		}
		}).on("open", [&broadcast_](websocket& ws) {
			std::cout << "opened" << std::endl;
		}).on("close", [&broadcast_](websocket& ws) {
			    auto tokenid = ws.get_user_data<std::shared_ptr<std::string>>("tokenid");
				std::cout << *tokenid << " has been closed: text, it's uuid: "<< ws.uuid() << std::endl;
				json root;
				root["type"] = "tip";
				root["message"] = broadcast_.getname(*tokenid) + " 退出文字聊天";
				broadcast_.broadcast(*tokenid, root.dump());
				broadcast_.remove_text(*tokenid);
		});
		server.router("/room", event);



		websocket_event eventImg;
		eventImg.on("message", [&broadcast_](websocket& ws) {
			auto msg_str = view2str(ws.messages());
			try {
				auto msg = json::parse(msg_str);
				auto type = msg["type"].get<std::string>();
				if (type == "ping") {
					return;
				}
				auto tokenid = msg["tokenid"].get<std::string>();
				if (type == "add") {
					auto name = msg["name"].get<std::string>();
					ws.set_user_data("tokenid", std::shared_ptr<std::string>(new std::string(tokenid)));
					broadcast_.addImg(tokenid, name, ws.shared_from_this());
				}
				else if (type == "chat") {
					broadcast_.broadcastImg(tokenid, msg_str);
				}
			}
			catch (std::exception const& ec) {
				std::cout << "json parse error" << std::endl;
				auto tmpoint = std::time(nullptr);
				std::ofstream file("./" + std::to_string(tmpoint) + "_error.json", std::ios::app);
				file << msg_str;
				file.close();
			}
			}).on("open", [&broadcast_](websocket& ws) {
				//std::cout << "opened" << std::endl;
			}).on("close", [&broadcast_](websocket& ws) {
				auto tokenid = ws.get_user_data<std::shared_ptr<std::string>>("tokenid");
				std::cout << *tokenid << " has been closed: img, its uuid: "<<ws.uuid() << std::endl;
					json root;
					root["type"] = "tip";
					root["message"] = broadcast_.getname_img(*tokenid) + " 退出图片聊天";
					broadcast_.broadcastImg(*tokenid, root.dump());
					broadcast_.remove_img(*tokenid);
			});
		server.router("/roomimg", eventImg);

		server.run();
		return 0;
}