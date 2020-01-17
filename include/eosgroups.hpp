



//it's the remote table struct that holds the config
struct groupconf{
    uint32_t inactivate_cust_after_sec;
    bool is_dac;
};
typedef eosio::singleton<"groupconf"_n, groupconf> groupconf_table;

class Config{
    private:
    groupconf groupconfig;

    public:
    //constructor

    Config(eosio::name contract, eosio::name scope){
    /*
        groupconf_table _groupconf(contract, scope.value);
        groupconfig = _groupconf.get();//singleton
        */
        groupconfig = {
            .inactivate_cust_after_sec = 60*60*30,
            .is_dac = false
        };
    }

    uint32_t get_inactivate_cust_after_sec(){
        return groupconfig.inactivate_cust_after_sec;
    }
    bool get_is_dac(){
        return groupconfig.is_dac;
    }
    //do other more advanced stuff here with the table data
};



