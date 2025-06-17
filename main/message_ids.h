#ifndef MESSAGE_IDS_H
#define MESSAGE_IDS_H

#define MSG_ID_HEARTBEAT        0
#define MSG_ID_BME280           1  
#define MSG_ID_TANK_LEVEL       2
#define MSG_ID_PUMP             3
#define MSG_ID_COMMS            4 
#define MSG_ID_24V_OUT          5
#define MSG_ID_BATT             6
#define MSG_ID_ANALOG_OUT       7
#define MSG_ID_420_INPUTS       8
#define MSG_ID_ANALOG_INPUTS    9
#define MSG_ID_PT1000           10
#define MSG_ID_STATUS           11
#define MSG_ID_SYSTEM           12



//Status notification messages
typedef enum { 
    PUMP_OK = 0,                 //goes in status data0
    PUMP_RUNNING, 
    PUMP_PURGING,
    PUMP_STOPPED,
    PUMP_WAITING_TO_START,
    AUTO_ROUTINE_CHECKING,
    AUTO_ROUTINE_FILLING,
    AUTO_ROUTINE_PURGING,
} status_messages_t;

//error messages
typedef enum {                  //goes in status data1
    ERROR_OK = 0,
    FILL_ERROR,
    EXT_TANK_ERROR,
} error_messages_t;


//CAN status messages
typedef enum {                  //goes in status data1
    CAN_OK = 0,
    CAN_INIT,
    CAN_ERROR,
    CAN_DATA,
} can_messages_t;

//System wake and sleep messages
typedef enum {                  //goes in system data0
    WAKE_UP = 0,
    GO_SLEEP,
} system_messages_t;



// Add additional message IDs here as needed

#endif // MESSAGE_IDS_H
