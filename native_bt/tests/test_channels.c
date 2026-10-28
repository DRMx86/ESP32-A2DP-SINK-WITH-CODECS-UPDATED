#include "dbt_channels.h"
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif
#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)
static dbt_acl_t acl;
static dbt_channels_t manager;
static uint32_t now;
static unsigned opened, closed, failed, reconfigured, data_calls, fixed_calls, parameter_calls;
static dbt_channel_reason_t last_reason;
static uint16_t last_result, data_size, data_cid;
static bool permit, idle, parameters;
static uint8_t wire[48], command[256], received_data[16];
static uint16_t wire_size;
static uint8_t wire_id;
static void w16(uint8_t *data, uint16_t value) { data[0]=(uint8_t)value; data[1]=(uint8_t)(value>>8); }
static uint16_t r16(const uint8_t *data) { return (uint16_t)(data[0]|((uint16_t)data[1]<<8)); }
static bool authorize(void *context,const dbt_link_t *link,uint16_t psm,dbt_security_t security) {
    (void)context;(void)link;(void)psm;(void)security;return permit;
}
static bool tx_idle(void *context,uint16_t handle,uint16_t cid) {(void)context;(void)handle;(void)cid;return idle;}
static bool le_parameters(void *context,const dbt_link_t *link,uint16_t minimum,uint16_t maximum,uint16_t latency,uint16_t timeout) {
    (void)context;(void)link;(void)minimum;(void)maximum;(void)latency;(void)timeout;++parameter_calls;return parameters;
}
static void event(void *context,dbt_channel_event_t kind,const dbt_channel_t *channel,dbt_channel_reason_t reason,uint16_t result) {
    (void)context;(void)channel;last_reason=reason;last_result=result;
    if(kind==DBT_CH_EVENT_OPEN)++opened;
    else if(kind==DBT_CH_EVENT_CLOSED)++closed;
    else if(kind==DBT_CH_EVENT_FAILED)++failed;
    else ++reconfigured;
}
static void data(void *context,const dbt_channel_t *channel,const uint8_t *bytes,uint16_t size) {
    (void)context;++data_calls;data_size=size;data_cid=channel->local_cid;
    for(unsigned i=0;i<size && i<sizeof(received_data);++i)received_data[i]=bytes[i];
}
static void fixed(void *context,const dbt_link_t *link,uint16_t cid,const uint8_t *bytes,uint16_t size) {
    (void)context;(void)link;(void)cid;(void)bytes;(void)size;++fixed_calls;
}
static dbt_result_t link_event(uint8_t code,const uint8_t *bytes,uint16_t size,dbt_link_change_t *change) {
    dbt_hci_packet_t packet={0};packet.type=4;packet.event=code;packet.payload=bytes;packet.size=size;
    return dbt_acl_event(&acl,&packet,change);
}
static void connect_link(uint16_t handle,bool le) {
    dbt_link_change_t change;
    uint8_t classic[11]={0};classic[1]=(uint8_t)handle;classic[9]=1;
    uint8_t low_energy[19]={1,0,0,0,0,0};low_energy[2]=(uint8_t)handle;
    (void)link_event(le?0x3e:3,le?low_energy:classic,le?19:11,&change);
}
static void fixture(void) {
    dbt_controller_info_t info={0};info.classic_acl_bytes=27;info.classic_acl_packets=3;
    info.le_acl_bytes=27;info.le_acl_packets=2;
    (void)dbt_acl_init(&acl,&info);connect_link(1,false);connect_link(2,false);connect_link(3,true);
    dbt_channel_hooks_t hooks={0};hooks.authorize=authorize;hooks.tx_idle=tx_idle;hooks.event=event;
    hooks.data=data;hooks.fixed_data=fixed;hooks.le_parameters=le_parameters;
    (void)dbt_channels_init(&manager,&acl,hooks,4096,1000,60000);
    permit=idle=parameters=true;opened=closed=failed=reconfigured=data_calls=fixed_calls=parameter_calls=0;
    now=0;wire_size=0;last_reason=DBT_CH_REASON_NORMAL;last_result=0;
}
static dbt_result_t rx(uint16_t handle,uint16_t cid,uint8_t code,uint8_t id,const uint8_t *body,uint16_t size) {
    command[0]=code;command[1]=id;w16(command+2,size);
    for(unsigned i=0;i<size;++i)command[4+i]=body[i];
    const dbt_link_t *link=dbt_acl_find(&acl,handle);
    return dbt_channels_receive(&manager,handle,link->generation,cid,command,(uint16_t)(4u+size));
}
static void poll(void) {dbt_channels_poll(&manager,++now,32);}
static int take(uint8_t code) {
    dbt_signal_view_t view;
    CHECK(dbt_channels_tx_peek(&manager,&view)==DBT_OK);
    CHECK(view.data[0]==code && view.size<=sizeof(wire));
    wire_size=view.size;wire_id=view.data[1];
    for(unsigned i=0;i<view.size;++i)wire[i]=view.data[i];
    return 0;
}
static int commit(void) {CHECK(dbt_channels_tx_commit(&manager,++now)==DBT_OK);return 0;}
static int connected_outgoing(uint16_t handle,uint16_t mtu,uint16_t peer,uint16_t *local,uint8_t *configuration_id) {
    CHECK(dbt_channels_open(&manager,handle,0x19,mtu,48,DBT_SECURITY_NONE,++now,local)==DBT_OK);
    int result=take(2);if(result)return result;
    CHECK(wire_size==8 && r16(wire+4)==0x19 && r16(wire+6)==*local);
    uint8_t id=wire_id;
    result=commit();if(result)return result;
    uint8_t response[8]={0};w16(response,peer);w16(response+2,*local);
    CHECK(rx(handle,1,3,id,response,8)==DBT_OK);poll();
    result=take(4);if(result)return result;
    CHECK(wire_size==12 && r16(wire+4)==peer && r16(wire+6)==0 &&
          wire[8]==1 && wire[9]==2 && r16(wire+10)==mtu);
    *configuration_id=wire_id;
    return commit();
}
static int open_channel(uint16_t handle,uint16_t mtu,uint16_t peer,uint16_t peer_mtu,uint16_t *local) {
    uint8_t config_id=0;int result=connected_outgoing(handle,mtu,peer,local,&config_id);if(result)return result;
    uint8_t peer_config[8]={0};w16(peer_config,*local);peer_config[4]=1;peer_config[5]=2;w16(peer_config+6,peer_mtu);
    CHECK(rx(handle,1,4,99,peer_config,8)==DBT_OK);poll();
    uint8_t own_response[6]={0};w16(own_response,*local);
    CHECK(rx(handle,1,5,config_id,own_response,6)==DBT_OK);poll();
    CHECK(dbt_channels_find(&manager,handle,*local)->state==DBT_CH_CONFIGURING);
    result=take(5);if(result)return result;
    CHECK(r16(wire+4)==peer && r16(wire+8)==0 && r16(wire+12)==peer_mtu);
    result=commit();if(result)return result;
    CHECK(dbt_channels_find(&manager,handle,*local)->state==DBT_CH_OPEN);
    return 0;
}

EXPORT int test_channels_outgoing_and_asymmetric_mtu(void) {
    fixture();uint16_t local=0;
    int result=open_channel(1,256,0x80,1000,&local);if(result)return result;
    const dbt_channel_t *channel=dbt_channels_find(&manager,1,local);
    CHECK(opened==1 && local==0x40 && channel->rx_mtu==256 && channel->tx_mtu==1000);
    dbt_l2cap_tx_t tx;uint8_t body[3]={1,2,3};
    CHECK(dbt_channels_prepare_data(&manager,1,local,body,3,&tx)==DBT_OK && tx.cid==0x80);
    CHECK(dbt_channels_prepare_data(&manager,1,local,body,1001,&tx)==DBT_NO_SPACE);
    CHECK(dbt_channels_receive(&manager,1,channel->link_generation,local,body,3)==DBT_OK);
    CHECK(data_calls==1 && data_cid==local && data_size==3 && received_data[2]==3);
    CHECK(dbt_channels_receive(&manager,1,channel->link_generation,local,body,257)==DBT_INVALID);
    return 0;
}

EXPORT int test_channels_incoming_and_duplicate_requests(void) {
    fixture();dbt_service_t service={0x19,1024,48,DBT_SECURITY_NONE};
    CHECK(dbt_channels_register(&manager,service)==DBT_OK);
    uint8_t request[4]={0x19,0,0xa0,0};
    CHECK(rx(1,1,2,7,request,4)==DBT_OK);request[0]=0;poll();
    int result=take(3);if(result)return result;
    CHECK(r16(wire+4)==0x40 && r16(wire+6)==0xa0 && r16(wire+8)==0);
    CHECK(!dbt_channels_find(&manager,1,0x40)->connection_ready);
    result=commit();if(result)return result;poll();
    result=take(4);if(result)return result;uint8_t config_id=wire_id;
    result=commit();if(result)return result;
    uint8_t peer_config[4]={0x40,0,0,0};
    CHECK(rx(1,1,4,8,peer_config,4)==DBT_OK);poll();
    result=take(5);if(result)return result;
    CHECK(r16(wire+4)==0xa0 && r16(wire+12)==672);
    result=commit();if(result)return result;
    uint8_t response[10]={0x40,0,0,0,0,0,1,2,0,2};
    CHECK(rx(1,1,5,config_id,response,10)==DBT_OK);poll();
    CHECK(opened==1 && dbt_channels_find(&manager,1,0x40)->rx_mtu==512);
    request[0]=0x19;
    CHECK(rx(1,1,2,7,request,4)==DBT_OK);poll();result=take(3);if(result)return result;
    CHECK(r16(wire+4)==0x40 && r16(wire+8)==0);result=commit();if(result)return result;
    CHECK(rx(1,1,4,8,peer_config,4)==DBT_OK);poll();result=take(5);if(result)return result;
    result=commit();if(result)return result;
    CHECK(opened==1 && reconfigured==0);
    unsigned count=0;for(unsigned i=0;i<DBT_CHANNEL_LIMIT;++i)count+=manager.channels[i].state!=DBT_CH_FREE;
    CHECK(count==1);
    return 0;
}

EXPORT int test_channels_option_errors_hints_and_continuation(void) {
    fixture();uint16_t local=0;uint8_t config_id=0;
    int result=connected_outgoing(1,256,0x80,&local,&config_id);if(result)return result;
    uint8_t options[8]={0x40,0,0,0,0x20,2,0x78,0x56};
    CHECK(rx(1,1,4,10,options,8)==DBT_OK);poll();result=take(5);if(result)return result;
    CHECK(r16(wire+8)==3 && wire[10]==0x20 && wire[11]==2 && wire[13]==0x56);
    result=commit();if(result)return result;
    options[4]=0xa0;
    CHECK(rx(1,1,4,11,options,8)==DBT_OK);poll();result=take(5);if(result)return result;
    CHECK(r16(wire+8)==0 && wire_size==14);result=commit();if(result)return result;
    uint8_t own_response[6]={0x40,0,0,0,0,0};
    CHECK(rx(1,1,5,config_id,own_response,6)==DBT_OK);poll();CHECK(opened==1);

    fixture();result=connected_outgoing(1,256,0x80,&local,&config_id);if(result)return result;
    uint8_t part1[8]={0x40,0,1,0,1,2,0x84,3}; /* peer RX=900, more options follow */
    CHECK(rx(1,1,4,20,part1,8)==DBT_OK);poll();result=take(5);if(result)return result;
    CHECK(r16(wire+6)==1 && r16(wire+8)==0);result=commit();if(result)return result;
    CHECK(!dbt_channels_find(&manager,1,local)->peer_configured);
    uint8_t part2[8]={0x40,0,0,0,2,2,0xff,0xff};
    CHECK(rx(1,1,4,21,part2,8)==DBT_OK);poll();result=take(5);if(result)return result;
    CHECK(r16(wire+6)==0 && r16(wire+12)==900);result=commit();if(result)return result;
    CHECK(rx(1,1,5,config_id,own_response,6)==DBT_OK);poll();
    CHECK(opened==1 && dbt_channels_find(&manager,1,local)->tx_mtu==900);
    return 0;
}

EXPORT int test_channels_response_continuation_and_mtu_retry(void) {
    fixture();uint16_t local=0;uint8_t config_id=0;
    int result=connected_outgoing(1,256,0x80,&local,&config_id);if(result)return result;
    uint8_t response[10]={0x40,0,1,0,0,0,1,2,128,0};
    CHECK(rx(1,1,5,config_id,response,10)==DBT_OK);poll();
    result=take(4);if(result)return result;CHECK(wire_size==8 && r16(wire+4)==0x80);
    config_id=wire_id;result=commit();if(result)return result;
    uint8_t final[6]={0x40,0,0,0,0,0};
    CHECK(rx(1,1,5,config_id,final,6)==DBT_OK);poll();
    CHECK(dbt_channels_find(&manager,1,local)->local_configured && dbt_channels_find(&manager,1,local)->rx_mtu==128);
    CHECK(opened==0);
    fixture();result=connected_outgoing(1,256,0x80,&local,&config_id);if(result)return result;
    response[2]=0;response[4]=1;
    CHECK(rx(1,1,5,config_id,response,10)==DBT_OK);poll();
    result=take(4);if(result)return result;CHECK(wire_size==12 && r16(wire+10)==128 && wire_id!=config_id);
    result=commit();if(result)return result;
    return 0;
}

EXPORT int test_channels_timers_pending_and_unsent_responses(void) {
    fixture();uint16_t local=0;dbt_signal_view_t view;
    CHECK(dbt_channels_open(&manager,1,0x19,256,48,DBT_SECURITY_NONE,0,&local)==DBT_OK);
    CHECK(dbt_channels_wait_ms(&manager,0)==1000);
    uint8_t response[8]={0x80,0,0x40,0,0,0,0,0};
    CHECK(rx(1,1,3,1,response,8)==DBT_OK);poll();
    CHECK(dbt_channels_find(&manager,1,local)->state==DBT_CH_CONNECTING && manager.unknown_responses==1);
    CHECK(dbt_channels_tx_peek(&manager,&view)==DBT_OK);
    CHECK(dbt_channels_tx_commit(&manager,500)==DBT_OK);
    dbt_channels_poll(&manager,1499,32);CHECK(failed==0);
    dbt_channels_poll(&manager,1500,32);CHECK(failed==1 && last_reason==DBT_CH_REASON_RESPONSE_TIMEOUT);

    fixture();CHECK(dbt_channels_open(&manager,1,0x19,256,48,DBT_SECURITY_NONE,0,&local)==DBT_OK);
    dbt_channels_poll(&manager,1000,32);CHECK(failed==1 && last_reason==DBT_CH_REASON_LOCAL_TIMEOUT);
    CHECK(dbt_channels_tx_peek(&manager,&view)==DBT_EMPTY);

    fixture();CHECK(dbt_channels_open(&manager,1,0x19,256,48,DBT_SECURITY_NONE,0,&local)==DBT_OK);
    CHECK(dbt_channels_tx_peek(&manager,&view)==DBT_OK);uint8_t id=view.data[1];
    CHECK(dbt_channels_tx_commit(&manager,0)==DBT_OK);
    uint8_t waiting[8]={0,0,0,0,1,0,1,0};
    CHECK(rx(1,1,3,id,waiting,8)==DBT_OK);dbt_channels_poll(&manager,1,32);
    for(uint32_t time=59000;time<=295000;time+=59000) {
        CHECK(rx(1,1,3,id,waiting,8)==DBT_OK);dbt_channels_poll(&manager,time,32);CHECK(failed==0);
    }
    dbt_channels_poll(&manager,300000,32);CHECK(failed==0);
    dbt_channels_poll(&manager,300001,32);CHECK(failed==1);
    return 0;
}

EXPORT int test_channels_multi_command_backpressure_and_malformed(void) {
    fixture();uint8_t cframe[48];
    for(unsigned i=0;i<12;++i) {cframe[4*i]=8;cframe[4*i+1]=(uint8_t)(i+1);cframe[4*i+2]=cframe[4*i+3]=0;}
    const dbt_link_t *link=dbt_acl_find(&acl,1);
    CHECK(dbt_channels_receive(&manager,1,link->generation,1,cframe,48)==DBT_OK);
    dbt_channels_poll(&manager,1,32);CHECK(manager.tx_count==8 && manager.rx_count==1);
    for(unsigned i=0;i<8;++i) {int result=take(9);if(result)return result;CHECK(wire_id==i+1);result=commit();if(result)return result;}
    poll();CHECK(manager.rx_count==0 && manager.tx_count==4);
    for(unsigned i=8;i<12;++i) {int result=take(9);if(result)return result;CHECK(wire_id==i+1);result=commit();if(result)return result;}
    cframe[4]=8;cframe[5]=30;cframe[6]=9;cframe[7]=0;
    CHECK(dbt_channels_receive(&manager,1,link->generation,1,cframe,8)==DBT_INVALID);
    CHECK(manager.rx_count==0 && manager.tx_count==0);
    CHECK(dbt_channels_receive(&manager,3,dbt_acl_find(&acl,3)->generation,5,cframe,8)==DBT_INVALID);
    return 0;
}

EXPORT int test_channels_generation_and_pinned_bytes(void) {
    fixture();uint16_t local=0;dbt_signal_view_t original,again;
    CHECK(dbt_channels_open(&manager,1,0x19,256,48,DBT_SECURITY_NONE,0,&local)==DBT_OK);
    CHECK(dbt_channels_tx_peek(&manager,&original)==DBT_OK);
    uint8_t saved[8];for(unsigned i=0;i<8;++i)saved[i]=original.data[i];
    uint8_t disconnected[4]={0,1,0,0x13};dbt_link_change_t change;
    CHECK(link_event(5,disconnected,4,&change)==DBT_OK);dbt_channels_link_change(&manager,&change);
    connect_link(1,false);
    CHECK(dbt_channels_open(&manager,1,0x19,256,48,DBT_SECURITY_NONE,1,&local)==DBT_OK);
    CHECK(dbt_channels_tx_peek(&manager,&again)==DBT_OK && again.generation==original.generation);
    for(unsigned i=0;i<8;++i)CHECK(saved[i]==again.data[i]);
    CHECK(dbt_channels_tx_cancel(&manager)==DBT_OK);
    CHECK(dbt_channels_tx_peek(&manager,&again)==DBT_OK && again.generation!=original.generation);
    CHECK(dbt_channels_receive(&manager,1,original.generation,1,saved,8)==DBT_INVALID);
    return 0;
}

EXPORT int test_channels_information_security_fixed_and_le(void) {
    fixture();uint8_t info[2]={2,0};
    CHECK(rx(1,1,0x0a,1,info,2)==DBT_OK);poll();int result=take(0x0b);if(result)return result;
    CHECK(r16(wire+6)==0 && wire[8]==0x80 && wire[9]==0);result=commit();if(result)return result;
    info[0]=3;CHECK(rx(1,1,0x0a,2,info,2)==DBT_OK);poll();result=take(0x0b);if(result)return result;
    CHECK(wire_size==16 && wire[8]==2);result=commit();if(result)return result;
    uint8_t bytes[2]={1,2};uint32_t generation=dbt_acl_find(&acl,3)->generation;
    CHECK(dbt_channels_receive(&manager,3,generation,4,bytes,2)==DBT_UNSUPPORTED);
    CHECK(dbt_channels_register_fixed(&manager,DBT_LINK_LE,4)==DBT_OK);
    CHECK(dbt_channels_receive(&manager,3,generation,4,bytes,2)==DBT_OK && fixed_calls==1);
    uint8_t update[8]={8,0,16,0,0,0,100,0};
    CHECK(rx(3,5,0x12,3,update,8)==DBT_OK);poll();result=take(0x13);if(result)return result;
    CHECK(r16(wire+4)==0 && parameter_calls==1);result=commit();if(result)return result;
    update[2]=0x80;update[3]=0x0c;update[6]=10;
    CHECK(rx(3,5,0x12,4,update,8)==DBT_OK);poll();result=take(0x13);if(result)return result;
    CHECK(r16(wire+4)==1 && parameter_calls==1);result=commit();if(result)return result;
    uint8_t connect[4]={0x19,0,0x80,0};
    CHECK(rx(3,5,2,5,connect,4)==DBT_OK);poll();result=take(1);if(result)return result;
    CHECK(r16(wire+4)==0);result=commit();if(result)return result;
    dbt_service_t service={0x19,256,48,DBT_SECURITY_AUTHENTICATED};
    CHECK(dbt_channels_register(&manager,service)==DBT_OK);manager.hooks.authorize=0;
    CHECK(rx(1,1,2,6,connect,4)==DBT_OK);poll();result=take(3);if(result)return result;
    CHECK(r16(wire+8)==3);result=commit();if(result)return result;
    return 0;
}

EXPORT int test_channels_reconfiguration_and_disconnect(void) {
    fixture();uint16_t local=0;int result=open_channel(1,256,0x80,1000,&local);if(result)return result;
    idle=false;uint8_t config[8]={0x40,0,0,0,1,2,0,2};
    CHECK(rx(1,1,4,44,config,8)==DBT_OK);poll();
    CHECK(manager.rx_deferred && manager.rx_count==1 && dbt_channels_wait_ms(&manager,now)==UINT32_MAX);
    uint8_t information[2]={2,0};CHECK(rx(2,1,0x0a,45,information,2)==DBT_OK);poll();
    result=take(0x0b);if(result)return result;result=commit();if(result)return result;
    CHECK(manager.rx_count==1 && manager.rx_deferred);
    idle=true;poll();result=take(5);if(result)return result;result=commit();if(result)return result;
    result=take(4);if(result)return result;uint8_t id=wire_id;result=commit();if(result)return result;
    uint8_t response[6]={0x40,0,0,0,0,0};
    CHECK(rx(1,1,5,id,response,6)==DBT_OK);poll();CHECK(reconfigured==1 && opened==1);
    CHECK(dbt_channels_find(&manager,1,local)->tx_mtu==512);
    CHECK(dbt_channels_close(&manager,1,local,++now)==DBT_OK);
    result=take(6);if(result)return result;id=wire_id;
    CHECK(r16(wire+4)==0x80 && r16(wire+6)==local);result=commit();if(result)return result;
    uint8_t wrong[4]={0x81,0,0x40,0};CHECK(rx(1,1,7,id,wrong,4)==DBT_OK);poll();CHECK(closed==0);
    wrong[0]=0x80;CHECK(rx(1,1,7,id,wrong,4)==DBT_OK);poll();
    CHECK(closed==1 && dbt_channels_find(&manager,1,local)==0);
    return 0;
}

EXPORT uint32_t test_channels_bytes(void) {return sizeof(manager);}

EXPORT int test_channels_limits_ids_and_protocol_rejections(void) {
    fixture();uint16_t local=0;uint8_t previous=0;
    for(unsigned i=0;i<270;++i) {
        CHECK(dbt_channels_open(&manager,1,0x19,256,48,DBT_SECURITY_NONE,++now,&local)==DBT_OK);
        int result=take(2);if(result)return result;CHECK(wire_id!=0 && wire_id!=previous);previous=wire_id;
        result=commit();if(result)return result;
        uint8_t refused[8]={0,0,0,0,2,0,0,0};
        CHECK(rx(1,1,3,previous,refused,8)==DBT_OK);poll();CHECK(dbt_channels_find(&manager,1,local)==0);
    }
    CHECK(failed==270);
    fixture();
    for(unsigned i=0;i<DBT_CHANNEL_LIMIT;++i) {
        CHECK(dbt_channels_open(&manager,1,0x19,256,48,DBT_SECURITY_NONE,++now,&local)==DBT_OK);
    }
    CHECK(dbt_channels_open(&manager,1,0x19,256,48,DBT_SECURITY_NONE,++now,&local)!=DBT_OK);
    for(unsigned i=0;i<DBT_CHANNEL_LIMIT;++i) {int result=take(2);if(result)return result;result=commit();if(result)return result;}
    dbt_service_t service={0x19,256,48,DBT_SECURITY_NONE};
    CHECK(dbt_channels_register(&manager,service)==DBT_OK);
    uint8_t request[4]={0x19,0,0x90,0};CHECK(rx(1,1,2,31,request,4)==DBT_OK);poll();
    int result=take(3);if(result)return result;CHECK(r16(wire+8)==4);result=commit();if(result)return result;
    fixture();uint8_t config_id=0;
    result=connected_outgoing(1,256,0x80,&local,&config_id);if(result)return result;
    uint8_t unsupported[15]={0x40,0,0,0,4,9,3,1,1,0,0,0,0,0,0};
    CHECK(rx(1,1,4,32,unsupported,15)==DBT_OK);poll();result=take(5);if(result)return result;
    CHECK(r16(wire+8)==1 && wire[10]==4 && wire[11]==9 && wire[12]==0);
    result=commit();if(result)return result;
    uint8_t bad_success[17]={0x40,0,0,0,0,0,4,9,3,0,0,0,0,0,0,0,0};
    CHECK(rx(1,1,5,config_id,bad_success,17)==DBT_OK);poll();
    CHECK(failed==1 && opened==0 && last_reason==DBT_CH_REASON_PROTOCOL);
    return 0;
}

EXPORT int test_channels_timer_wrap_and_late_pending_bound(void) {
    fixture();uint16_t local=0;dbt_signal_view_t view;
    CHECK(dbt_channels_open(&manager,1,0x19,256,48,DBT_SECURITY_NONE,UINT32_MAX-499u,&local)==DBT_OK);
    CHECK(dbt_channels_tx_peek(&manager,&view)==DBT_OK);
    uint8_t id=view.data[1];CHECK(dbt_channels_tx_commit(&manager,UINT32_MAX-499u)==DBT_OK);
    dbt_channels_poll(&manager,499,32);CHECK(failed==0);
    dbt_channels_poll(&manager,500,32);CHECK(failed==1);
    fixture();CHECK(dbt_channels_open(&manager,1,0x19,256,48,DBT_SECURITY_NONE,0,&local)==DBT_OK);
    CHECK(dbt_channels_tx_peek(&manager,&view)==DBT_OK);id=view.data[1];
    CHECK(dbt_channels_tx_commit(&manager,0)==DBT_OK);
    uint8_t waiting[8]={0,0,0,0,1,0,0,0};
    CHECK(rx(1,1,3,id,waiting,8)==DBT_OK);dbt_channels_poll(&manager,1,32);
    CHECK(rx(1,1,3,id,waiting,8)==DBT_OK);dbt_channels_poll(&manager,300001,32);
    CHECK(failed==1 && dbt_channels_find(&manager,1,local)==0);
    return 0;
}
