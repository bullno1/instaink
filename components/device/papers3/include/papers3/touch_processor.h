#ifndef PAPERS3_TOUCH_PROCESSOR_H
#define PAPERS3_TOUCH_PROCESSOR_H

#include "gt911.h"
#include <stdbool.h>
#include <stdint.h>

#define TOUCH_INVALID_ID 0xFF

typedef enum {
    TOUCH_DOWN,
    TOUCH_UP,
    TOUCH_MOVE,
} touch_event_type_t;

typedef struct {
    touch_event_type_t type;
    uint8_t            id;      /* GT911 track ID                  */
    uint16_t           x;
    uint16_t           y;
    uint16_t           size;    /* contact area, 0 on TOUCH_UP     */
} touch_event_t;

typedef void (*touch_event_cb_t)(const touch_event_t *event, void *user_data);

typedef struct {
    bool     active;
    uint8_t  track_id;
    uint16_t last_x;
    uint16_t last_y;
	uint8_t  missing_frames;
} touch_slot_t;

typedef struct {
    touch_event_cb_t cb;
    void            *user_data;
    touch_slot_t     slots[GT911_MAX_POINTS];
} touch_processor_t;

void touch_processor_init(touch_processor_t *state,
                          touch_event_cb_t   cb,
                          void              *user_data);

void touch_processor_update(touch_processor_t   *state,
                            const gt911_state_t *raw);

#endif
