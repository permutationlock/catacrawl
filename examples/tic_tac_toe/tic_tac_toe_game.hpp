#ifndef TIC_TAC_TOE_HPP
#define TIC_TAC_TOE_HPP

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <functional>

using std::vector;
using std::unordered_map;
using std::unordered_set;
using std::queue;

const int X_VAL = 1;
const int O_VAL = -1;
const int EMPTY_VAL = 0;

class tic_tac_toe_board {
public:
  tic_tac_toe_board(): m_state(0), m_move_count(0) {
    for(std::size_t i=0; i<9; i++) {
      m_board.push_back(0);
    }
  }

  bool add_x(unsigned int i, unsigned int j) {
    if(i > 2 || j > 2) {
      return false;
    } else if(get_value(i, j) != EMPTY_VAL) {
      return false;
    }

    move(i, j, X_VAL);
    return true;
  }

  bool add_o(unsigned int i, unsigned int j) {
    if(i > 2 || j > 2) {
      return false;
    } else if(get_value(i, j) != EMPTY_VAL) {
      return false;
    }

    move(i, j, O_VAL);
    return true;
  }

  int get_state() const {
    return m_state;
  }

  bool is_done() const {
    return (m_move_count == 9) || (get_state() != 0);
  }

  const std::vector<int>& get_board() const {
    return m_board;
  }
  
private:
  int get_value(int i, int j) {
    return m_board[i+3*j];
  }

  void set_value(int i, int j, int s) {
    m_board[i+3*j] = s;
  }

  void move(int x, int y, int s){
    set_value(x, y, s);
    m_move_count++;

    //check col
    for(std::size_t i = 0; i < 3; i++){
      if(get_value(x, i) != s) {
        break;
      }
      if(i == 2){
        m_state = s;
        return;
      }
    }

    //check row
    for(std::size_t i = 0; i < 3; i++){
      if(get_value(i, y) != s) {
        break;
      }
      if(i == 2){
        m_state = s;
        return;
      }
    }

    //check diagonal
    if(x == y){
      for(std::size_t i = 0; i < 3; i++){
        if(get_value(i, i) != s) {
          break;
        }
        if(i == 2){
          m_state = s;
          return;
        }
      }
    }

    //check anti-diagonal
    if(x + y == 2){
      for(std::size_t i = 0; i < 3; i++){
        if(get_value(i, 2-i) != s) {
          break;
        }
        if(i == 2){
          m_state = s;
          return;
        }
      }
    }
  }

  // member variables
  vector<int> m_board;
  int m_state;
  std::size_t m_move_count;
};

struct tic_tac_toe_player_traits {
  struct id {
    using player_id = unsigned long;
    using session_id = unsigned long;

    struct hash {
      std::size_t operator()(const id& id_data) const {
        return std::hash<player_id>{}(id_data.player)
          ^ std::hash<session_id>{}(id_data.session);
      }

      std::size_t operator()(unsigned long int_id) const {
        return std::hash<unsigned long>{}(int_id);
      }
    };

    id() {}
    id(player_id p, session_id s) : player(p), session(s) {}

    bool operator==(const id& other_id) const {
      return (player == other_id.player) && (session == other_id.session);
    }

    player_id player;
    session_id session;
  };

  static id::player_id parse_player_id(const json& id_json) {
    return id_json.get<id::player_id>();
  }

  static id::session_id parse_session_id(const json& id_json) {
    return id_json.get<id::session_id>();
  }
};

class tic_tac_toe_game {
public:
  using player_traits = tic_tac_toe_player_traits;
  using player_id = player_traits::id::player_id;
  using message = std::pair<player_id, std::string>;

  tic_tac_toe_game(const json& msg) : m_valid(true), m_started(false),
    m_game_over(false), m_xmove(true), m_state(0), m_xtime(100000),
    m_otime(100000), m_elapsed_time(0)
  {
    bool is_matched = false;
    try {
      is_matched = msg.at("matched").get<bool>();
    } catch(json::exception& e) {
      m_valid = false;
    }

    if(!is_matched) {
      m_valid = false;
    }
  }
  
  void connect(vector<message>& out_messages, player_id id) {
    spdlog::trace("tic tac toe connect player {}", id);

    if(m_data_map.count(id) == 0) {
      m_player_list.push_back(id);
    }

    m_data_map[id].is_connected = true;

    if(m_started) {
      out_messages.emplace_back(id, get_game_state(id).dump());
    }
  }

  void disconnect(vector<message>& out_messages, player_id id) {
    m_data_map[id].is_connected = false;
  }

  void update(
      vector<message>& out_messages,
      const vector<message>& in_messages,
      long delta_time
    )
  {
    if(m_started && !m_game_over) {
      if(m_xmove) {
        m_xtime -= delta_time;
      } else {
        m_otime -= delta_time;
      }

      if(m_xtime <= 0) {
        m_xtime = 0;
        m_state = -1;
        m_game_over = true;
      } else if(m_otime <= 0) {
        m_otime = 0;
        m_state = 1;
        m_game_over = true;
      }

      m_elapsed_time += delta_time;
      if(m_elapsed_time >= 1000) {
        for(player_id player : m_player_list) {
          if(m_data_map[player].is_connected) {
            out_messages.emplace_back(player, get_time_state(player).dump());
          }
        }
        m_elapsed_time = 0;
      }

      if(is_done()) {
        for(player_id player : m_player_list) {
          if(m_data_map[player].is_connected) {
            out_messages.emplace_back(player, get_game_state(player).dump());
          }
        }
      }

      for(const message& msg : in_messages) {
        json msg_json = json::parse(msg.second, nullptr, false);
        if(msg_json.is_discarded()) {
          spdlog::debug(
              "player {} sent invalid json: {}", msg.first, msg.second
            );
        } else {
          player_update(out_messages, msg.first, msg_json);
        }
      }
    } else {
      if(m_valid && (m_player_list.size() > 1)) {
        m_started = true;

        for(player_id player : m_player_list) {
          if(m_data_map[player].is_connected) {
            out_messages.emplace_back(player, get_game_state(player).dump());
          }
        }
      }
    }
  }

  json get_state() const {
    json game_json;
    game_json["board"] = m_board.get_board();
    game_json["xmove"] = m_xmove;
    game_json["moves"] = m_move_list;
    game_json["times"] = std::vector<long>{ m_xtime, m_otime };
    game_json["state"] = m_board.get_state() + m_state;
    game_json["done"] = is_done();
 
    return game_json;
  }

  bool is_done() const {
    return m_board.is_done() || m_game_over;
  }
  
  bool is_valid() const {
    return m_valid;
  }

private:
  void player_update(
      vector<message>& msg_list,
      player_id id,
      const json& data
    )
  {
    try {
      unsigned int i = data["move"][0].get<unsigned int>();
      unsigned int j = data["move"][1].get<unsigned int>();

      if(m_started && !is_done()) {
        if(id == m_player_list[0]) {
          if(m_xmove) {
            if(m_board.add_x(i, j)) {
              m_xmove = false;
              m_move_list.push_back(data["move"]);
              for(player_id player : m_player_list) {
                if(m_data_map[player].is_connected) {
                  msg_list.emplace_back(player, get_game_state(player).dump());
                }
              }
            } else {
              spdlog::debug(
                  "player {} sent invalid move: {}", id, data.dump()
                );
            }
          } else {
            spdlog::debug(
                "player {} sent move out of turn: {}", id, data.dump()
              );
          }
        } else if(id == m_player_list[1]) {
          if(!m_xmove) {
            if(m_board.add_o(i, j)) {
              m_xmove = true;
              m_move_list.push_back(data["move"]);
              for(player_id player : m_player_list) {
                if(m_data_map[player].is_connected) {
                  msg_list.emplace_back(player, get_game_state(player).dump());
                }
              }
            } else {
              spdlog::debug(
                  "player {} sent invalid move: {}", id, data.dump()
                );
            }
          } else {
            spdlog::debug(
                "player {} sent move out of turn: {}", id, data.dump()
              );
          }
        } else {
          spdlog::error(
              "player {} sent move but isn't in the game: {}",
              id,
              data.dump()
            );
        }
      }
    } catch(json::exception& e) {
      spdlog::error("player {} sent invalid json", id);
    }
  }

  json get_game_state(player_id id) const {
    bool isx = (id == m_player_list.front());

    json game_json;
    game_json["type"] = "game";
    game_json["board"] = m_board.get_board();
    game_json["time"] = (isx ? m_xtime : m_otime);
    game_json["opponent_time"] = (isx ? m_otime : m_xtime); 
    game_json["xmove"] = m_xmove;
    game_json["state"] = m_board.get_state() + m_state;
    game_json["done"] = is_done();
    game_json["your_turn"] = isx ? m_xmove : !m_xmove;
 
    return game_json;
  }

  json get_time_state(player_id id) const {
    bool isx = (id == m_player_list.front());

    json game_json;

    game_json["type"] = "time";
    game_json["time"] = (isx ? m_xtime : m_otime);
    game_json["opponent_time"] = (isx ? m_otime : m_xtime); 
    return game_json;
  }

  struct player_data {
    player_data() : has_connected(false), is_connected(false) {}
    bool has_connected;
    bool is_connected;
  };

  vector<player_id> m_player_list;
  unordered_map<player_id, player_data> m_data_map;

  bool m_valid;
  bool m_started;
  bool m_game_over;
  bool m_xmove;
  int m_state;
  long m_xtime;
  long m_otime;
  long m_elapsed_time;

  vector<json> m_move_list;

  tic_tac_toe_board m_board;
};

class tic_tac_toe_matchmaker {
public:
  using player_traits = tic_tac_toe_player_traits;
  using session_id = player_traits::id::session_id;
  using id_hash = player_traits::id::hash;
  using message = std::pair<session_id, std::string>;
  using game = std::tuple<std::vector<session_id>, session_id, json>;

  struct session_data {
    session_data(const json& data) {}

    bool is_valid() {
      return true;
    }
  };

  tic_tac_toe_matchmaker() : m_sid_count(0) {}

  bool can_match(
      const unordered_map<session_id, session_data, id_hash>& session_map
    )
  {
    return session_map.size() > 1;
  }

  void match(
      vector<game>& game_list,
      vector<message>& messages,
      const unordered_map<session_id, session_data, id_hash>& session_map,
      long delta_time
    )
  {
    vector<session_id> sl;
    for(auto& spair : session_map) {
      sl.push_back(spair.first);
      if(sl.size() > 1) {
        game_list.emplace_back(
            std::move(sl), 
            m_sid_count++,
            json{ { "matched", true } }
          );
      }
    }
  }

  json get_cancel_data() const {
    json temp;
    temp["matched"] = false;
    return temp; 
  }

private:
  session_id m_sid_count;
};

#endif // TIC_TAC_TOE_HPP
