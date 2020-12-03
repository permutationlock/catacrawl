#ifndef GAME_SERVER_HPP
#define GAME_SERVER_HPP

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <jwt-cpp/jwt.h>

#include <spdlog/spdlog.h>

#include <iostream>
#include <set>
#include <mutex>
#include <condition_variable>
#include <chrono>

namespace jwt_game_server {
  using namespace std::chrono_literals;

  using websocketpp::connection_hdl;

  // Define functional types
  using std::bind;
  using std::placeholders::_1;
  using std::placeholders::_2;

  // Define data types for set, vector, map, shared_ptr
  using std::set;
  using std::vector;
  using std::map;
  using std::shared_ptr;
  using std::make_shared;

  // Define threading data types
  using std::set;
  using std::queue;
  using std::mutex;
  using std::lock_guard;
  using std::unique_lock;
  using std::condition_variable;

  // Minimum time between game updates
  constexpr std::chrono::milliseconds timestep(500ms);

  // websocket server type
  typedef websocketpp::server<websocketpp::config::asio> wss_server;

  // on_message action for the server
  enum action_type {
    SUBSCRIBE,
    UNSUBSCRIBE,
    MESSAGE
  };

  struct action {
    action(action_type t, connection_hdl h) : type(t), hdl(h) {}
    action(action_type t, connection_hdl h, wss_server::message_ptr m)
      : type(t), hdl(h), msg(m) {}

    action_type type;
    connection_hdl hdl;
    wss_server::message_ptr msg;
  };

  template<typename game_data, typename json_traits>
  class game_instance {
  public:
    typedef typename game_data::player_id player_id;

    game_instance(wss_server* s, const game_data& data) :
      m_server_ptr(s), m_game(data)  {}

    void connect(player_id id, connection_hdl hdl) {
      spdlog::trace("connect called for player {}", id);
      lock_guard<mutex> guard(m_game_lock);
      m_player_connections[id] = hdl;
      if(m_player_status.count(id) < 1 || !m_player_status[id]) {
        m_player_status[id] = true;
        m_game.connect(id);
      }
    }

    void disconnect(player_id id) {
      spdlog::trace("disconnect called for player {}", id);
      lock_guard<mutex> guard(m_game_lock);
      m_player_status[id] = false;
      m_game.disconnect(id);
    }

    bool is_connected(player_id id) {
      lock_guard<mutex> guard(m_game_lock);
      return m_player_status[id];
    }

    connection_hdl get_connection(player_id id) {
      lock_guard<mutex> guard(m_game_lock);
      return m_player_connections[id];
    }

    void process_player_update(player_id id, const std::string& text) {  
      spdlog::trace("player_update called for player {}", id);
      typename json_traits::json msg_json;

      try {
        json_traits::parse(msg_json, text);
      } catch(std::exception& e) {
        spdlog::debug("update message from {} was not valid json", id);
        return;
      }

      lock_guard<mutex> guard(m_game_lock);
      m_game.player_update(id, msg_json);
      send_messages();
    }
    
    // update game every timestep, return false if game is over
    // this method should only be called by the game update thread
    bool game_update(long delta_time) {
      spdlog::trace("update with timestep: {}", delta_time);
      
      lock_guard<mutex> guard(m_game_lock);
      m_game.game_update(delta_time);
      send_messages();

      return m_game.is_done();
    }

    // return player ids
    vector<player_id> get_player_list() {
      lock_guard<mutex> guard(m_game_lock);
      return m_game.get_player_list();
    }

    ~game_instance() {
      spdlog::debug("destructed game");
    }

  private:
    // private members don't lock since public members already acquire the lock
    void broadcast(const std::string& text) {
      for (auto const& player : m_player_connections) {
        player_id id = player.first;
        connection_hdl hdl = player.second;
        if(m_player_status[id]) {
          m_server_ptr->send(hdl, text,
              websocketpp::frame::opcode::text);
        }
      }
    }

    bool send(player_id id, const std::string& text) {
      bool success = true;
      if(m_player_status[id]) {
        m_server_ptr->send(
            m_player_connections[id],
            text,
            websocketpp::frame::opcode::text
          );
      } else {
        success = false;
      }
      return success;
    }

    void send_messages() {
      spdlog::trace("sending queued messages");
      while(m_game.has_message()) {
        auto msg = m_game.get_message();
        if(msg.broadcast) {
          broadcast(msg.text);
        } else {
          send(msg.id, msg.text);
        }
        m_game.pop_message();
      }
    }

    wss_server* m_server_ptr;
    map<player_id, connection_hdl> m_player_connections;
    map<player_id, bool> m_player_status;
    mutex m_game_lock;
    game_data m_game;
  };

  template<typename game_data, typename jwt_clock, typename json_traits>
  class game_server {
  public:
    game_server(const jwt::verifier<jwt_clock, json_traits>& v) : m_jwt_verifier(v) {
      // initialize Asio Transport
      m_server.init_asio();

      // register handler callbacks
      m_server.set_open_handler(bind(&game_server::on_open, this,
        jwt_game_server::_1));
      m_server.set_close_handler(bind(&game_server::on_close, this,
        jwt_game_server::_1));
      m_server.set_message_handler(
          bind(&game_server::on_message, this, jwt_game_server::_1,
            jwt_game_server::_2)
        );
    }

    void run(uint16_t port) {
      // listen on specified port
      m_server.listen(port);

      // start the server accept loop
      m_server.start_accept();

      // start the ASIO io_service run loop
      try {
        m_server.run();
      } catch (std::exception & e) {
        spdlog::debug(e.what());
      }
    }

    void on_open(connection_hdl hdl) {
      {
        //lock_guard<mutex> guard(m_action_lock);
        spdlog::debug("on_open");
        //m_actions.push(action(SUBSCRIBE,hdl));
      }
      //m_action_cond.notify_one();
    }

    void on_close(connection_hdl hdl) {
      {
        lock_guard<mutex> guard(m_action_lock);
        spdlog::debug("on_close");
        m_actions.push(action(UNSUBSCRIBE,hdl));
      }
      m_action_cond.notify_one();
    }

    void on_message(connection_hdl hdl, wss_server::message_ptr msg) {
      // queue message up for sending by processing thread
      {
        lock_guard<mutex> guard(m_action_lock);
        spdlog::debug("on_message: {}", msg->get_payload());
        m_actions.push(action(MESSAGE,hdl,msg));
      }
      m_action_cond.notify_one();
    }

    void setup_player(connection_hdl hdl, const std::string& token) {
      typename json_traits::json login_json;
      try {
        jwt::decoded_jwt<json_traits> decoded_token =
          jwt::decode<json_traits>(token);
        auto claim_map = decoded_token.get_payload_claims();
        login_json = claim_map.at("game_data").to_json();
        m_jwt_verifier.verify(decoded_token);
      } catch(std::out_of_range& e) {
        spdlog::debug("connection provided jwt without gam_data claim");
      } catch(jwt::error::token_verification_exception& e) { 
          spdlog::debug("connection provided jwt that could not be verified");
      } catch(std::invalid_argument& e) { 
        spdlog::debug("connection provided invalid jwt token string");
      } catch(std::runtime_error& e) {
        spdlog::debug("connection provided invalid json in jwt");
      } catch(std::exception& e) {
        spdlog::debug("something else went wrong in login");
      }

      game_data d(login_json);

      if(d.is_valid()) {
        player_id id = d.get_creator_id();
        {
          lock_guard<mutex> connection_guard(m_connection_lock);
          m_connection_ids[hdl] = id;
          spdlog::debug("assigning connection to id: {}", id);
        }
        player_connect(hdl, d);
      } else {
        spdlog::debug("connection provided incorrect login json");
      }
    }

    void process_messages() {
      while(1) {
        unique_lock<mutex> action_lock(m_action_lock);

        while(m_actions.empty()) {
          m_action_cond.wait(action_lock);
        }

        action a = m_actions.front();
        m_actions.pop();

        action_lock.unlock();

        if (a.type == SUBSCRIBE) {
          spdlog::trace("processing SUBSCRIBE action");
        } else if (a.type == UNSUBSCRIBE) {
          spdlog::trace("processing UNSUBSCRIBE action");
          unique_lock<mutex> player_lock(m_connection_lock);
          if(m_connection_ids.count(a.hdl) < 1) {
            spdlog::debug("player disconnected without providing id");
          } else {
            // connection provided a player id
            player_lock.unlock();
            player_disconnect(a.hdl);
          }
        } else if (a.type == MESSAGE) {
          spdlog::trace("processing MESSAGE action");
          unique_lock<mutex> player_lock(m_connection_lock);

          if(m_connection_ids.count(a.hdl) < 1) {
            spdlog::debug("recieved message from connection w/no id");
            player_lock.unlock();

            // player id not setup, must be a login message
            setup_player(a.hdl, a.msg->get_payload());
          } else {
            // player id already setup, route to their game
            player_id id = m_connection_ids[a.hdl];
            player_lock.unlock();

            spdlog::debug("received message from id: {}", id);

            lock_guard<mutex> game_guard(m_game_list_lock);
            if(m_player_games.count(id)) {
              m_player_games[id]->process_player_update(id,
                  a.msg->get_payload());
            } else {
              spdlog::error("player {} does not have a game", id);
            }
          }
        } else {
          // undefined.
        }
      }
    }

    void player_connect(connection_hdl hdl, const game_data& data) {
      lock_guard<mutex> game_guard(m_game_list_lock);
      player_id main_id = data.get_creator_id();
      if(m_player_games.count(main_id)<1) {
        auto gs =
          make_shared<game_instance<game_data, json_traits> >(&m_server, data);
        m_games.insert(gs);

        for(player_id id : data.get_player_list()) {
          m_player_games[id] = gs;
        }

        m_player_games[main_id]->connect(main_id, hdl);
      } else {
        if(m_player_games[main_id]->is_connected(main_id)) {
          lock_guard<mutex> connection_guard(m_connection_lock);
          connection_hdl hdl =
            m_player_games[main_id]->get_connection(main_id);
          m_connection_ids.erase(hdl);
          m_server.close(
              hdl,
              websocketpp::close::status::normal,
              "player connected again"
            );
          spdlog::debug("terminating redundant connection for player {}",
            main_id);
        }
        m_player_games[main_id]->connect(main_id, hdl);
      }
    }

    void player_disconnect(connection_hdl hdl) {
      player_id id;
      {
        lock_guard<mutex> connection_guard(m_connection_lock);
        id = m_connection_ids[hdl];
        m_connection_ids.erase(hdl);
      }
      lock_guard<mutex> game_guard(m_game_list_lock);
      m_player_games[id]->disconnect(id);
      m_player_games.erase(id);

      spdlog::debug("player {} disconnected", id);
    }

    void update_games() {
      auto time_start = std::chrono::high_resolution_clock::now();
      while(1) { 
        auto delta_time =
          std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - time_start);
        if(delta_time >= timestep) {
          lock_guard<mutex> guard(m_game_list_lock);
          time_start = std::chrono::high_resolution_clock::now();
          
          // consider using std::execution::par_unseq here !!
          for(auto it = m_games.begin(); it != m_games.end();) {
            shared_ptr<game_instance<game_data, json_traits> > game = *it;

            if(game->game_update(delta_time.count())) {
              it++;
            } else {
              spdlog::debug("game ended");
              vector<player_id> ids = game->get_player_list();
              for(player_id id : ids) {
                connection_hdl hdl = game->get_connection(id);

                m_server.close(
                    hdl,
                    websocketpp::close::status::normal,
                    "game ended"
                  );
              }
              spdlog::trace("erasing game from list");
              it = m_games.erase(it);
            }
          }
        }
        std::this_thread::sleep_for(std::min(1ms, timestep-delta_time));
      }
    }

  private:
    typedef typename game_data::player_id player_id;

    typedef set<connection_hdl,std::owner_less<connection_hdl> > con_set;
    typedef map<
        connection_hdl,
        player_id,
        std::owner_less<connection_hdl>
      > con_map;

    wss_server m_server;

    mutex m_action_lock;
    mutex m_game_list_lock;
    mutex m_connection_lock;
    condition_variable m_action_cond;
    condition_variable m_match_cond;

    queue<action> m_actions;
   
    con_map m_connection_ids;
    map<player_id, shared_ptr<game_instance<game_data, json_traits> > >
      m_player_games;

    set<shared_ptr<game_instance<game_data, json_traits> > > m_games;

    jwt::verifier<jwt_clock, json_traits> m_jwt_verifier;
  };
}

#endif // GAME_SERVER_HPP
