#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include <prinit.h>
#include <nss.h>
#include <pk11func.h>
#include <secmod.h>


#define MYSLOTMAP_NUM 255

struct myslotmapentry {
    int slot_id;
    PK11SlotInfo * card;
};

typedef struct myslotmapentry myslotmap[MYSLOTMAP_NUM];

void myslotmap_init(myslotmap map) {
    int i;

    for (i = 0; i < MYSLOTMAP_NUM; i++) {
        map[i].slot_id = 0;
        map[i].card = NULL;
    }
}

int myslotmap_insert(myslotmap map, int slot_id, PK11SlotInfo * card) {
    int i;

    // First find an empty spot to insert into
    for (i = 0; i < MYSLOTMAP_NUM; i++) {
        if (map[i].card == NULL) {
            map[i].slot_id = slot_id;
            map[i].card = card;

            return 0;
        }
    }

    // Out of space
    return 1;
}

PK11SlotInfo * myslotmap_get(myslotmap map, int slot_id) {
    int i;

    for (i = 0; i < MYSLOTMAP_NUM; i++) {
        if (map[i].card != NULL && map[i].slot_id == slot_id) {
            return map[i].card;
        }
    }

    return NULL;
}

int myslotmap_remove(myslotmap map, int slot_id) {
    int i;

    for (i = 0; i < MYSLOTMAP_NUM; i++) {
        if (map[i].card != NULL && map[i].slot_id == slot_id) {
            map[i].slot_id = 0;
            map[i].card = NULL;
        }

        return 0;
    }

    return 1;
}


typedef struct {
    NSSInitContext * nss_context;
    SECMODModule * driver;
    myslotmap slot_map;
} Tester;


void load_nss(Tester * self)
{
        NSSInitContext *context = NULL;

        /* The first field in the NSSInitParameters structure
         * is the size of the structure. NSS requires this, so
         * that it can change the size of the structure in future
         * versions of NSS in a detectable way
         */
        NSSInitParameters parameters = { sizeof (parameters), };
        const uint32_t flags = NSS_INIT_READONLY
            | NSS_INIT_FORCEOPEN
            | NSS_INIT_NOROOTINIT
            | NSS_INIT_OPTIMIZESPACE
            | NSS_INIT_PK11RELOAD;

        printf("attempting to load NSS database '%s'\n", GSD_SMARTCARD_MANAGER_NSS_DB);

        PR_Init(PR_USER_THREAD, PR_PRIORITY_NORMAL, 0);

        context = NSS_InitContext(
                GSD_SMARTCARD_MANAGER_NSS_DB,
                "",
                "",
                SECMOD_DB,
                &parameters,
                flags
        );

        if (context == NULL) {
            printf("Bad db context\n");
            exit(2);
        }

        printf("NSS database '%s' loaded\n", GSD_SMARTCARD_MANAGER_NSS_DB);
        self->nss_context = context;
}


void activate_all_drivers(Tester * self) {
    SECMODListLock * lock;
    SECMODModuleList * driver_list, * node;

    lock = SECMOD_GetDefaultModuleListLock();
    if (lock == NULL) {
        exit(1);
    }

    SECMOD_GetReadLock(lock);
    driver_list = SECMOD_GetDefaultModuleList();
    self->driver = NULL;
    for (node = driver_list; node != NULL; node = node->next) {
        printf("Found driver: '%s'\n", node->module->commonName);
        if (!node->module->loaded)
            continue;

        if (!SECMOD_HasRemovableSlots(node->module))
            continue;

        if (node->module->dllName == NULL)
            continue;

        printf("Loaded driver: '%s'\n", node->module->commonName);
        self->driver = node->module;
    }
    SECMOD_ReleaseReadLock(lock);
}


void sync_initial_tokens(Tester * self) {
    int i;

    for (i = 0; i < self->driver->slotCount; i++) {
        PK11SlotInfo * card;

        card = self->driver->slots[i];

        if (PK11_IsPresent(card)) {
            CK_SLOT_ID slot_id;
            slot_id = PK11_GetSlotID(card);

            printf("Detected smartcard in slot %lu!\n", slot_id);
        }
    }
}


int watch_one_event(Tester * self) {
    PK11SlotInfo *card, *old_card;
    CK_SLOT_ID slot_id;
    int old_slot_series = -1, slot_series;

    card = SECMOD_WaitForAnyTokenEvent(self->driver, CKF_DONT_BLOCK, PR_SecondsToInterval(1));

    if (card == NULL) {
        printf("SECMOD_WaitForAnyTokenEvent nonblock\n");
        return 0;
    }

    slot_id = PK11_GetSlotID(card);
    slot_series = PK11_GetSlotSeries(card);
    old_card = myslotmap_get(self->slot_map, slot_id);
    if (old_card != NULL) {
        old_slot_series = PK11_GetSlotSeries(old_card);
        if (old_slot_series != slot_series) {
            // Missed removal
            // printf("Missed removal of card: '%s'\n", old_card->name);
            printf("Missed removal of card: '%s:%s'\n", PK11_GetTokenName(old_card), PK11_GetSlotName(old_card));
        }
        myslotmap_remove(self->slot_map, slot_id);
    }

    if (PK11_IsPresent(card)) {
        // printf("Card '%s' inserted into slot %d\n", card->name, slot_id);
        printf("Card '%s:%s' inserted into slot %ld\n", PK11_GetTokenName(card), PK11_GetSlotName(card), slot_id);
        myslotmap_insert(self->slot_map, slot_id, PK11_ReferenceSlot(card));
    } else if (old_card == NULL) {
        printf("Slot %lu is empty\n", slot_id);
    } else {
        // printf("Detected removal event for card '%s' in slot %d\n", card->name, slot_id);
        printf("Detected removal event for card '%s:%s' in slot %lu\n", PK11_GetTokenName(card), PK11_GetSlotName(card), slot_id);
    }

    PK11_FreeSlot(card);
    return 1;
}


void watch_smartcards(Tester * self) {
    int got_event;

    printf("Watching for events...\n");
    while (1) {
        got_event = watch_one_event(self);
        if (!got_event) {
            usleep(1000000);
        }
    }
}


int main() {
    Tester _tester;
    Tester * tester = &_tester;

    myslotmap_init(tester->slot_map);
    printf("TEST!\n");

    load_nss(tester);

    activate_all_drivers(tester);
    if (tester->driver == NULL) {
        printf("No driver loaded\n");
        exit(4);
    }

    watch_smartcards(tester);

    return 0;
}
