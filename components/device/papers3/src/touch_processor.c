#include <papers3/touch_processor.h>
#include <esp_log.h>

#define TOUCH_LIFT_DEBOUNCE 3

static const char *TAG = "touch_processor";

void touch_processor_init(touch_processor_t *state,
                          touch_event_cb_t   cb,
                          void              *user_data)
{
    state->cb        = cb;
    state->user_data = user_data;

    for (int i = 0; i < GT911_MAX_POINTS; i++) {
        state->slots[i].active   = false;
        state->slots[i].track_id = TOUCH_INVALID_ID;
        state->slots[i].last_x   = 0;
        state->slots[i].last_y   = 0;
    }
}

/* Find the slot currently tracking a given track ID, or -1 */
static int find_slot_by_id(touch_processor_t *state, uint8_t track_id)
{
    for (int i = 0; i < GT911_MAX_POINTS; i++) {
        if (state->slots[i].active &&
            state->slots[i].track_id == track_id) {
            return i;
        }
    }
    return -1;
}

/* Find a free slot, or -1 */
static int find_free_slot(touch_processor_t *state)
{
    for (int i = 0; i < GT911_MAX_POINTS; i++) {
        if (!state->slots[i].active) {
            return i;
        }
    }
    return -1;
}

void touch_processor_update(touch_processor_t  *state,
                            const gt911_state_t *touch)
{
    /* Mark which track IDs are present in this frame */
    bool seen[GT911_MAX_POINTS] = {false};

    /* Process each reported point */
    for (uint8_t i = 0; i < touch->count; i++) {
        const gt911_point_t *p  = &touch->points[i];
        int                  si = find_slot_by_id(state, p->id);

        if (si < 0) {
            /* New track ID — find a free slot and fire TOUCH_DOWN */
            si = find_free_slot(state);
            if (si < 0) {
                ESP_LOGW(TAG, "No free slot for track ID %d", p->id);
                continue;
            }

            state->slots[si].active   = true;
            state->slots[si].track_id = p->id;
            state->slots[si].last_x   = p->x;
            state->slots[si].last_y   = p->y;

            touch_event_t event = {
                .type = TOUCH_DOWN,
                .id   = p->id,
                .x    = p->x,
                .y    = p->y,
                .size = p->size,
            };
            state->cb(&event, state->user_data);

        } else {
            /* Known track ID — check for movement */
            touch_slot_t *slot = &state->slots[si];

            if (p->x != slot->last_x || p->y != slot->last_y) {
                touch_event_t event = {
                    .type = TOUCH_MOVE,
                    .id   = p->id,
                    .x    = p->x,
                    .y    = p->y,
                    .size = p->size,
                };
                state->cb(&event, state->user_data);

                slot->last_x = p->x;
                slot->last_y = p->y;
            }
        }

        seen[si] = true;
    }

    /* Check for lifted fingers — debounced */
    for (int i = 0; i < GT911_MAX_POINTS; i++) {
        if (!state->slots[i].active) continue;

        if (seen[i]) {
            /* Present this frame — reset counter */
            state->slots[i].missing_frames = 0;
        } else {
            /* Absent this frame — increment counter */
            state->slots[i].missing_frames++;

            if (state->slots[i].missing_frames >= TOUCH_LIFT_DEBOUNCE) {
                touch_event_t event = {
                    .type = TOUCH_UP,
                    .id   = state->slots[i].track_id,
                    .x    = state->slots[i].last_x,
                    .y    = state->slots[i].last_y,
                    .size = 0,
                };
                state->cb(&event, state->user_data);

                state->slots[i].active         = false;
                state->slots[i].missing_frames = 0;
            }
        }
	}
}
