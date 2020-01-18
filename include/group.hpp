
#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/system.hpp>
#include <eosio/permission.hpp>
#include <eosio/singleton.hpp>
#include <system_structs.hpp>

#include <eosgroups.hpp>

//#include <eosio/print.hpp>

#include <math.h>

using namespace std;
using namespace eosio;

CONTRACT group : public contract {
  public:
    using contract::contract;

    struct action_threshold{
      name action_name;
      name threshold_name;
    };

    struct user_agreement{
      string md5_hash;
      string markdown_url;
      time_point_sec last_update;
    };

    struct groupstate{
      uint8_t cust_count;
      uint64_t member_count;
    };

    //json
    /*
        {
          "max_custodians":0,
          "inactivate_cust_after_sec":86400,
          "exec_on_threshold_zero":false,
          "proposal_archive_size":3,
          "member_registration":true,
          "withdrawals": true,
          "internal_transfers": false,
          "deposits": false,
          "maintainer_account": "piecesnbitss"
        }
    */

    struct groupconf{
      uint8_t max_custodians = 0;
      uint32_t inactivate_cust_after_sec = 60*60*24*30;
      bool exec_on_threshold_zero = false;
      uint8_t proposal_archive_size = 3;
      bool member_registration = false;
      bool withdrawals = false;
      bool internal_transfers = false;
      bool deposits = false;
      name maintainer_account = name("eosgroups222");
      //user_agreement user_agreement;
    };

    ACTION invitecust(name account);
    ACTION removecust(name account);
    ACTION setcusts(vector<name> accounts);

    ACTION propose(name proposer, string title, string description, vector<action> actions, time_point_sec expiration);
    ACTION approve(name approver, uint64_t id);
    ACTION unapprove(name unapprover, uint64_t id);
    ACTION cancel(name canceler, uint64_t id);
    ACTION exec(name executer, uint64_t id);
    ACTION trunchistory(name archive_type, uint32_t batch_size);

    ACTION widthdraw(name account, extended_asset amount);
    ACTION internalxfr(name from, name to, extended_asset amount, string msg);
    ACTION imalive(name account);
    ACTION spawnchildac(name new_account, asset ram_amount, asset net_amount, asset cpu_amount, name parent, name module_name);
    ACTION addchildac(name account, name parent, name module_name);
    ACTION remchildac(name account);

    ACTION manthreshold(name threshold_name, int8_t threshold, bool remove);
    //ACTION manactlinks(name contract, vector<action_threshold> new_action_thresholds);//will be deprecated
    ACTION manthreshlin(name contract, name action_name, name threshold_name, bool remove);

    ACTION regmember(name actor);
    ACTION unregmember(name actor);

    ACTION updateconf(groupconf new_conf, bool remove);

    //notification handlers
    [[eosio::on_notify("*::transfer")]]
    void on_transfer(name from, name to, asset quantity, string memo);


  private:
  
    struct threshold_name_and_value{
      name threshold_name;
      uint8_t threshold;
    };


    TABLE coreconf{
      groupconf conf;
    };
    typedef eosio::singleton<"coreconf"_n, coreconf> coreconf_table;

    TABLE corestate{
      groupstate state;
    };
    typedef eosio::singleton<"corestate"_n, corestate> corestate_table;

    TABLE threshlinks {
      uint64_t id;
      name contract;
      name action_name;
      name threshold_name;

      auto primary_key() const { return id; }
      //uint64_t by_action() const { return action_name.value; }
      uint64_t by_threshold() const { return threshold_name.value; }
      uint128_t by_cont_act() const { return (uint128_t{contract.value} << 64) | action_name.value; }
    };
    typedef multi_index<name("threshlinks"), threshlinks,
      //eosio::indexed_by<"byaction"_n, eosio::const_mem_fun<threshlinks, uint64_t, &threshlinks::by_action>>,
      eosio::indexed_by<"bythreshold"_n, eosio::const_mem_fun<threshlinks, uint64_t, &threshlinks::by_threshold>>,
      eosio::indexed_by<"bycontact"_n, eosio::const_mem_fun<threshlinks, uint128_t, &threshlinks::by_cont_act>>
    > threshlinks_table;

    TABLE thresholds {
      name threshold_name;
      int8_t threshold;
      auto primary_key() const { return threshold_name.value; }
    };
    typedef multi_index<name("thresholds"), thresholds> thresholds_table;

    TABLE proposals {
      uint64_t id;
      string title;
      string description;
      name proposer;
      vector<action> actions;
      time_point_sec submitted;
      time_point_sec expiration;
      vector<name> approvals;
      name required_threshold;
      name last_actor;
      checksum256 trx_id;

      auto primary_key() const { return id; }
      uint64_t by_threshold() const { return required_threshold.value; }
      uint64_t by_proposer() const { return proposer.value; }
      uint64_t by_expiration() const { return expiration.sec_since_epoch(); }
    };
    typedef multi_index<name("proposals"), proposals,
      eosio::indexed_by<"bythreshold"_n, eosio::const_mem_fun<proposals, uint64_t, &proposals::by_threshold>>,
      eosio::indexed_by<"byproposer"_n, eosio::const_mem_fun<proposals, uint64_t, &proposals::by_proposer>>,
      eosio::indexed_by<"byexpiration"_n, eosio::const_mem_fun<proposals, uint64_t, &proposals::by_expiration>>
    > proposals_table;

    TABLE custodians {
      name account;
      name authority = name("active");
      uint8_t weight = 1;
      time_point_sec joined = time_point_sec(current_time_point().sec_since_epoch());
      time_point_sec last_active;

      auto primary_key() const { return account.value; }
      uint64_t by_last_active() const { return last_active.sec_since_epoch(); }
    };
    typedef multi_index<name("custodians"), custodians,
      eosio::indexed_by<"bylastactive"_n, eosio::const_mem_fun<custodians, uint64_t, &custodians::by_last_active>>
    > custodians_table;

    TABLE members {
      name account;
      time_point_sec agreement_date;
      uint64_t r1;
      uint64_t r2;
      auto primary_key() const { return account.value; }

    };
    typedef multi_index<name("members"), members> members_table;

    //scoped table
    TABLE balances {
      extended_asset balance;
      uint64_t primary_key()const { return balance.quantity.symbol.raw(); }
    };
    typedef multi_index<"balances"_n, balances> balances_table;

    TABLE childaccounts {
      name account_name;
      name parent;
      name module_name;
      auto primary_key() const { return account_name.value; }
      uint64_t by_module_name() const { return module_name.value; }
    };
    typedef multi_index<name("childaccount"), childaccounts,
      eosio::indexed_by<"bymodulename"_n, eosio::const_mem_fun<childaccounts, uint64_t, &childaccounts::by_module_name>>
    > childaccounts_table;

    //functions//
    groupconf get_group_conf();
    bool is_account_voice_wrapper(const name& account);
    void update_owner_maintainance(const name& maintainer);
    //action whitelist stuff
    void update_whitelist_action(const name& contract, const name& action_name, const name& threshold_name);
    void remove_whitelist_action(const name& contract, const name& action_name);
    bool is_action_whitelisted(const name& contract, const name& action_name);

    //internal threshold system
    bool is_existing_threshold_name(const name& threshold_name);//not used
    uint8_t get_threshold_by_name(const name& threshold_name);
    void insert_or_update_or_delete_threshold(const name& threshold_name, const int8_t& threshold, const bool& remove, const bool& privileged);
    void update_thresholds_based_on_number_custodians();
    threshold_name_and_value get_required_threshold_name_and_value_for_contract_action(const name& contract, const name& action_name);
    bool is_threshold_linked(const name& threshold_name);

    //vector<threshold_name_and_value> get_counts_for
    //https://eosio.stackexchange.com/questions/4999/how-do-i-pass-an-iterator/5012#5012
    uint8_t get_total_approved_proposal_weight(proposals_table::const_iterator& prop_itr);
    

    //custodians
    bool is_custodian(const name& account, const bool& update_last_active, const bool& check_if_alive);
    void update_custodian_weight(const name& account, const uint8_t& weight);
    void update_active();
    void update_custodian_last_active(const name& account);
    bool is_account_alive(time_point_sec last_active);

    //internal accounting
    void sub_balance(const name& account, const extended_asset& value);
    void add_balance(const name& account, const extended_asset& value);

    //proposals
    void delete_proposal(const uint64_t& id);
    void approve_proposal(const uint64_t& id, const name& approver);
    void assert_invalid_authorization( vector<permission_level> auths);
    void archive_proposal(const name& archive_type, proposals_table& idx, proposals_table::const_iterator& prop_itr);

    //members
    bool is_member(const name& accountname);
    bool member_has_balance(const name& accountname);
    void update_member_count(int delta);

    //messaging to parent contract
    void add_system_msg(const name& group_name_self, const name& receiver, const string& msg, const uint8_t type);

    template <typename T>
    void cleanTable(name code, uint64_t account, const uint32_t batchSize){
      T db(code, account);
      uint32_t counter = 0;
      auto itr = db.begin();
      while(itr != db.end() && counter++ < batchSize) {
          itr = db.erase(itr);
      }
    }
    checksum256 get_trx_id(){
      auto size = transaction_size();
      char* buffer = (char*)( 512 < size ? malloc(size) : alloca(size) );
      uint32_t read = read_transaction( buffer, size );
      check( size == read, "ERR::READ_TRANSACTION_FAILED::read_transaction failed");
      checksum256 trx_id = sha256(buffer, read);
      return trx_id;
    }


};


