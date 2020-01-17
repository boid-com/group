    
namespace groupconfig{ 

  struct groupconf{
    uint32_t inactivate_cust_after_sec = 60*60*30;
    bool is_dac = false;
  };

  groupconf conf;

  uint32_t get_inactivate_cust_after_sec(){
    return conf.inactivate_cust_after_sec;
  }

}