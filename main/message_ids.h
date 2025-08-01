#ifndef MESSAGE_IDS_H
#define MESSAGE_IDS_H

#define MSG_ID_HEARTBEAT        0
#define MSG_ID_BME280           1  
#define MSG_ID_TANK_LEVEL       2
#define MSG_ID_MODE             3
#define MSG_ID_COMMS            4 
#define MSG_ID_SPARE            5
#define MSG_ID_BATT             6
#define MSG_ID_OUTPUTS          7
#define MSG_ID_420_INPUTS       8
#define MSG_ID_ANALOG_INPUTS    9
#define MSG_ID_PT1000           10
#define MSG_ID_STATUS           11
#define MSG_ID_SYSTEM           12
#define MSG_ID_SETTINGS         13


//Message types
typedef enum {
    MSG_TYPE_COMMAND = 0,      //goes in message data0
    MSG_TYPE_DATA,             //goes in message data0
} message_type_t;

//Mode messages
typedef enum { 
    MODE_MANUAL = 0,           //goes in mode data0
    MODE_AUTO, 
} mode_messages_t;


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
    PUMP_ERROR,
    
    
} status_messages_t;

//error messages
typedef enum {                  //goes in status data1
    ERROR_OK = 0,
    FILL_ERROR,
    COMM_ERROR,
    
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



//Comms button messages
typedef enum {
    RUN = 0,
    STOP,
    RESET,
} button_messages_t;


#endif // MESSAGE_IDS_H
