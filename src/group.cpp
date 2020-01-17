#include <group.hpp>
#include <functions.cpp>

ACTION group::updateconf(groupconf new_conf, bool remove){
    require_auth(get_self());

    coreconf_table _coreconf(get_self(), get_self().value);
    if(remove){
      _coreconf.remove();
      return;
    }
    auto conf = _coreconf.get_or_default(coreconf());
    if(conf.conf.maintainer_account != new_conf.maintainer_account){
      update_owner_maintainance(new_conf.maintainer_account);
    }
    conf.conf = new_conf;
    _coreconf.set(conf, get_self());
}


///////////////////////////////////
ACTION group::propose(name proposer, string title, string description, vector<action> actions, time_point_sec expiration) {
  require_auth(proposer);
  check(is_custodian(proposer, true, true), "You can't propose group actions because you are not a custodian.");
  time_point_sec now = time_point_sec(current_time_point());

  //validate actions
  check(actions.size() > 0 && actions.size() < 8, "Number of actions not allowed.");

  //find  max required threshold + assert when get_self@owner isn't in the authorization
  threshold_name_and_value max_required_threshold;
  for (std::vector<int>::size_type i = 0; i != actions.size(); i++){
    threshold_name_and_value tnav = get_required_threshold_name_and_value_for_contract_action(actions[i].account, actions[i].name);
    check(tnav.threshold >= 0, "Action is blocked via negative threshold");
    if(i==0){
      max_required_threshold = tnav;
    }
    else if(tnav.threshold > max_required_threshold.threshold){
      max_required_threshold = tnav;
    }
  }

  groupconf conf = get_group_conf();
  if(max_required_threshold.threshold == 0 && conf.exec_on_threshold_zero ){
    //immediate execution, no signatures needed
    for(action act : actions) { 
        act.send();
    }
    return;
  }

  //validate expiration
  check( now < expiration, "Expiration must be in the future.");
  uint32_t seconds_left = expiration.sec_since_epoch() - now.sec_since_epoch();
  check(seconds_left >= 60*60, "Minimum expiration not met.");

  name ram_payer = get_self();

  proposals_table _proposals(get_self(), get_self().value);
  _proposals.emplace(ram_payer, [&](auto& n) {
    n.id = _proposals.available_primary_key();
    n.proposer = proposer;
    n.actions = actions;
    n.expiration = expiration;
    n.submitted = now;
    n.description = description;
    n.title = title;
    n.last_actor = proposer;
    n.trx_id = get_trx_id();
    n.required_threshold = max_required_threshold.threshold_name;
  });
}
//////////////
ACTION group::approve(name approver, uint64_t id) {
  require_auth(approver);
  check(is_custodian(approver, true, true), "You can't approve because you are not a custodian.");
  proposals_table _proposals(get_self(), get_self().value);
  auto prop_itr = _proposals.find(id);
  check(prop_itr != _proposals.end(), "Proposal not found.");

  std::set<name> new_approvals{};
  new_approvals.insert(approver);
  for (name old_approver: prop_itr->approvals) {
    //check for dups and clean out non/old custodians.
    //todo calculate current weight here
    if(is_custodian(old_approver, false, false)){
      check(new_approvals.insert(old_approver).second, "You already approved this proposal.");
    }
  }
  _proposals.modify( prop_itr, same_payer, [&]( auto& n) {
      n.last_actor = approver;
      n.approvals = vector<name>(new_approvals.begin(), new_approvals.end() );
  });
}


ACTION group::unapprove(name unapprover, uint64_t id) {
  require_auth(unapprover);
  check(is_custodian(unapprover, true, true), "You can't unapprove because you are not a custodian.");
  proposals_table _proposals(get_self(), get_self().value);
  auto prop_itr = _proposals.find(id);
  check(prop_itr != _proposals.end(), "Proposal not found.");

  std::set<name> new_approvals{};
  bool has_approved = false;
  for (name old_approver: prop_itr->approvals) {
    //check for dups and clean out non/old custodians.
    if(old_approver == unapprover ){
      has_approved = true;
    }
    else if(is_custodian(old_approver, false, false) ){
      new_approvals.insert(old_approver);
    }
  }
  check(has_approved, "You are not in the list of approvals.");
  _proposals.modify( prop_itr, same_payer, [&]( auto& n) {
      n.approvals = vector<name>(new_approvals.begin(), new_approvals.end() );
      n.last_actor = unapprover;
  });
}



ACTION group::cancel(name canceler, uint64_t id) {
  require_auth(canceler);
  proposals_table _proposals(get_self(), get_self().value);
  auto prop_itr = _proposals.find(id);
  check(prop_itr != _proposals.end(), "Proposal not found.");
  check(prop_itr->proposer == canceler, "This is not your proposal.");
  _proposals.modify( prop_itr, same_payer, [&]( auto& n) {
      n.last_actor = canceler;
  });
  archive_proposal(name("cancelled"), _proposals, prop_itr);
  is_custodian(canceler, true, true);//this will update the timestamp if canceler is (still) custodian
}

ACTION group::exec(name executer, uint64_t id) {
  require_auth(executer);
  proposals_table _proposals(get_self(), get_self().value);
  auto prop_itr = _proposals.find(id);
  check(prop_itr != _proposals.end(), "Proposal not found.");
  time_point_sec now = time_point_sec(current_time_point());

  check( now < prop_itr->expiration, "Proposal Expired.");

  //verify if can be executed -> highest threshold met?
  uint8_t total_approved_weight = get_total_approved_proposal_weight(prop_itr);

  uint8_t highest_action_threshold = get_threshold_by_name(prop_itr->required_threshold);
  
  check(total_approved_weight >= highest_action_threshold, "Not enough vote weight for execution.");

  //exec
  for(action act : prop_itr->actions) { 
      act.send();
  }

  _proposals.modify( prop_itr, same_payer, [&]( auto& n) {
      n.last_actor = executer;
  });

  archive_proposal(name("executed"), _proposals, prop_itr);
  //_proposals.erase(prop_itr);

  is_custodian(executer, true, true);//this will update the timestamp if canceler is (still) custodian
}

ACTION group::invitecust(name account){
  require_auth(get_self() );
  check(account != get_self(), "Self can't be a custodian.");
  
  auto conf = get_group_conf();

  check(is_account_voice_wrapper(account), "Account does not exist or doesn't meet requirements.");

  custodians_table _custodians(get_self(), get_self().value);
  auto cust_itr = _custodians.find(account.value);

  check(cust_itr == _custodians.end(), "Account already a custodian.");

  _custodians.emplace( get_self(), [&]( auto& n){
      n.account = account;
  });
  //update_active();
}

ACTION group::removecust(name account){
  require_auth(get_self());

  custodians_table _custodians(get_self(), get_self().value);
  auto cust_itr = _custodians.find(account.value);

  check(cust_itr != _custodians.end(), "Account is not a custodian.");
    
  _custodians.erase(cust_itr);
  
  if(_custodians.begin() != _custodians.end() ){
    //the erased entry was not the last one.
    update_active();
  }
  else{
    check(false, "Can't remove the last custodian.");
  }
}

ACTION group::imalive(name account){
  require_auth(account);
  custodians_table _custodians(get_self(), get_self().value);
  auto cust_itr = _custodians.find(account.value);
  check(cust_itr != _custodians.end(), "You are not a custodian, no proof of live needed.");
  if(!is_account_alive(cust_itr->last_active) ){
    update_custodian_last_active(account);
    update_active();
  }
  else{
    update_custodian_last_active(account);
  }
}

ACTION group::setcusts(vector<name> accounts){
  
  require_auth(get_self() );//this needs to be the voting contract
  int count_new = accounts.size();
  auto conf = get_group_conf();
  //accounts size must be <= numberofcustodians.
  //
  check(count_new != 0, "Empty custodian list not allowed");

  custodians_table _custodians(get_self(), get_self().value);
  //intersect
  sort(accounts.begin(), accounts.end() );
  vector<custodians> new_custs;
}

ACTION group::widthdraw(name account, extended_asset amount) {
  require_auth(account);
  check(get_group_conf().withdrawals, "Withdrawals are disabled");
  check(account != get_self(), "Can't withdraw to self.");
  check(amount.quantity.amount > 0, "Amount must be greater then zero.");

  //sub_balance(account, amount); this is handled by the on_notify !!!
  
  action(
    permission_level{get_self(), "owner"_n},
    amount.contract, "transfer"_n,
    make_tuple(get_self(), account, amount.quantity, string("withdraw from user account"))
  ).send();
  
}

ACTION group::internalxfr(name from, name to, extended_asset amount, string msg){
  require_auth(from);
  groupconf conf = get_group_conf();
  check(conf.internal_transfers, "Internal transfers are disabled.");
  check(is_member(from), "Sender must be a member." );
  check(is_member(to), "Receiver must be a member." );
  check(amount.quantity.amount > 0, "Transfer value must be greater then zero.");
  sub_balance(from, amount);
  add_balance(to, amount);
}

ACTION group::manthreshold(name threshold_name, int8_t threshold, bool remove){
  require_auth(get_self() );
  insert_or_update_or_delete_threshold(threshold_name, threshold, remove, false);//!!!!!!!!!!!!! false
}

ACTION group::manthreshlin(name contract, name action_name, name threshold_name, bool remove){
  require_auth(get_self() );
  check(contract != name(0) && action_name != name(0), "Invalid link parameters.");
  check(threshold_name != name(0), "Threshold name can't be empty.");
  check(threshold_name != name("default"), "Default threshold can't be assigned.");

  if(contract != name(0) ){
    check(is_account(contract), "Contract isn't an existing account.");
  }
  check(is_existing_threshold_name(threshold_name), "Threshold name doesn't exist. Create it first.");

  threshlinks_table _threshlinks(get_self(), get_self().value);

  auto by_cont_act = _threshlinks.get_index<"bycontact"_n>();
  uint128_t composite_id = (uint128_t{contract.value} << 64) | action_name.value;
  auto link_itr = by_cont_act.find(composite_id);

  if(link_itr != by_cont_act.end() ){
    //link already exists so modify or remove
    if(remove){
      by_cont_act.erase(link_itr);
    }
    else{
     check(link_itr->action_name != action_name, "Action or contract already linked with this threshold");
      by_cont_act.modify( link_itr, same_payer, [&]( auto& n) {
          n.action_name = action_name;
          n.threshold_name = threshold_name;
      });   
    }
  }
  else{
    //new link so add it to the table
    if(remove){
      check(false, "Can't remove a non existing threshold link.");
    }
    _threshlinks.emplace( get_self(), [&]( auto& n){
      n.id = _threshlinks.available_primary_key();
      n.contract = contract;
      n.action_name = action_name;
      n.threshold_name = threshold_name;
    });
  }
}

ACTION group::trunchistory( name archive_type, uint32_t batch_size){
  require_auth(get_self() );
  check(archive_type != get_self(), "Not allowed to clear this scope.");
  proposals_table h_proposals(get_self(), archive_type.value);
  check(h_proposals.begin() != h_proposals.end(), "History scope empty.");

  uint32_t counter = 0;
  auto itr = h_proposals.begin();
  while(itr != h_proposals.end() && counter++ < batch_size) {
    itr = h_proposals.erase(itr);
  }
}

ACTION group::regmember(name actor){
  require_auth(actor);
  groupconf conf = get_group_conf();
  check(conf.member_registration, "Member registration is disabled.");
  check(is_account_voice_wrapper(actor), "Accountname not eligible for registering as member.");
  check(actor != get_self(), "Contract can't be a member of itself.");
  members_table _members(get_self(), get_self().value);
  auto mem_itr = _members.find(actor.value);
  check(mem_itr == _members.end(), "Accountname already a member.");

  _members.emplace( actor, [&]( auto& n){
    n.account = actor;
  });
  update_member_count(1);
}


ACTION group::unregmember(name actor){
  require_auth(actor);
  check(!member_has_balance(actor),"Member has positive balance, withdraw first.");
  members_table _members(get_self(), get_self().value);
  auto mem_itr = _members.find(actor.value);
  check(mem_itr != _members.end(), "Accountname is not a member.");
  _members.erase(mem_itr);
  update_member_count(-1);
}



ACTION group::spawnchildac(name new_account, asset ram_amount, asset net_amount, asset cpu_amount, name parent, name module_name){
    require_auth(get_self() );
    check(!is_account(new_account), "The chosen accountname is already taken.");

    asset total_value = ram_amount + net_amount + cpu_amount;
    extended_asset extended_total_value = extended_asset(total_value, name("eosio.token") );
    sub_balance( get_self(), extended_total_value);

    parent = parent == name(0) ? get_self() : parent;

    bool transfer_bw = false;

    vector<eosiosystem::permission_level_weight> controller_accounts_active;
    vector<eosiosystem::permission_level_weight> controller_accounts_owner;


    permission_level parent_active{parent, "active"_n};
    //permission_level code_permission{new_account, name("eosio.code") };


    eosiosystem::permission_level_weight pmlw_parent_active{
        .permission = parent_active,
        .weight = (uint16_t) 1,
    };

   /* eosiosystem::permission_level_weight pmlw_eosiocode{
        .permission = code_permission,
        .weight = (uint16_t) 1,
    };
    controller_accounts_owner.push_back(pmlw_eosiocode);
    */

    controller_accounts_owner.push_back(pmlw_parent_active);
    controller_accounts_active.push_back(pmlw_parent_active);

    eosiosystem::authority active_authority{
        .threshold = 1,
        .accounts = controller_accounts_active,
        .waits = {},
        .keys = {}
    };

    eosiosystem::authority owner_authority{
        .threshold = 1,
        .accounts = controller_accounts_owner,
        .waits = {},
        .keys = {}
    };

    //create new account
    action(
        permission_level{ get_self(), "owner"_n },
        "eosio"_n,
        "newaccount"_n,
        std::make_tuple(get_self(), new_account, owner_authority, active_authority )
    ).send();

    //buy resources
    //ram_amount + net_amount + cpu_amount
    action(
        permission_level{ get_self(), "owner"_n },
        "eosio"_n,
        "delegatebw"_n,
        std::make_tuple(get_self(), new_account, net_amount, cpu_amount, transfer_bw )
    ).send();

    action(
        permission_level{ get_self(), "owner"_n },
        "eosio"_n,
        "buyram"_n,
        std::make_tuple(get_self(), new_account, ram_amount )
    ).send();
    //add new account to the child account table
    action(
        permission_level{ get_self(), "owner"_n },
        get_self(),
        "addchildac"_n,
        std::make_tuple(new_account, parent, module_name )
    ).send();
}

ACTION group::addchildac(name account, name parent, name module_name){
  require_auth(get_self() );
  check(is_account(account), "The account doesn't exist.");
  //check if account already a child
  childaccounts_table _childaccounts(get_self(), get_self().value);
  auto itr = _childaccounts.find(account.value);
  check(itr == _childaccounts.end(), "Account is already registered as child.");

  //check if parent is valid
  parent = parent == name(0) ? get_self() : parent;
  if(parent != get_self() ){
    itr = _childaccounts.find(parent.value);
    check(itr != _childaccounts.end(), "Parent account is not a child.");
  }

  //check if module name already exists
  if(module_name != name(0) ){
    auto by_module_name = _childaccounts.get_index<"bymodulename"_n>();
    auto itr2 = by_module_name.find(module_name.value);
    check(itr2 == by_module_name.end(), "Duplicate module name.");
  }

  _childaccounts.emplace( get_self(), [&]( auto& n){
      n.account_name = account;
      n.parent = parent;
      n.module_name = module_name;
  });

}

ACTION group::remchildac(name account){
  require_auth(get_self() );
  childaccounts_table _childaccounts(get_self(), get_self().value);
  auto itr = _childaccounts.find(account.value);
  check(itr != _childaccounts.end(), "Account is not a child.");
  _childaccounts.erase(itr);
}

//notify transfer handler
void group::on_transfer(name from, name to, asset quantity, string memo){

  check(quantity.amount > 0, "Transfer amount must be greater then zero.");
  check(to != from, "Invalid transfer");

  extended_asset extended_quantity = extended_asset(quantity, get_first_receiver());
  groupconf conf = get_group_conf();
  //////////////////////
  //incomming transfers
  //////////////////////
  if (to == get_self() ) {
    //check memo if it's a transfer to top up a user wallet
    if(memo.substr(0, 21) == "add to user account: " ){
      check(conf.deposits, "Deposits to user accounts is disabled.");
      string potentialaccountname = memo.length() >= 22 ? memo.substr(21, 12 ) : "";
      check(is_member(name(potentialaccountname) ), "Receiver in memo is not a registered member.");
      add_balance( name(potentialaccountname), extended_quantity);
      return;
    }
    else{
      //fund group wallet
      add_balance( to, extended_quantity); //to == self
      return;   
    }
  }
  //////////////////////
  //outgoing transfers
  //////////////////////
  if (from == get_self() ) {
    //check memo if it is a user withrawal
    if(memo.substr(0, 26) == "withdraw from user account" ){
      print("user withdraw");
      check(is_member(to), "To is not an regstered member.");
      sub_balance( to, extended_quantity);
      return;
    }
    else{
      sub_balance( from, extended_quantity);
      return;    
    }
  }
}




