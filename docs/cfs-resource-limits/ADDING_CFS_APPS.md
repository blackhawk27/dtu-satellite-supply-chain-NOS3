# Guide: Adding NASA cFS Apps to NOS3

When integrating external NASA cFS (core Flight System) apps into the NOS3 environment, you must handle the source code carefully to avoid Git tracking errors (like empty "gitlinks") and ensure compatibility with the specific cFE version running in NOS3. 

Follow these steps for every new cFS app you add.

## 1. Downloading and Integrating the Source Code

If you clone a GitHub repository directly into your existing `nos3` repository without removing its `.git` directory, Git will treat the new app as a submodule reference (a "gitlink"). This will cause other developers to see an empty directory with missing files when they pull your changes.

To prevent this, always strip the external Git tracking data before moving the files into their final location.

**The Installation Procedure:**

```bash
# 1. Navigate to the apps directory
cd nos3/fsw/apps

# 2. Clone the target repo into a temporary directory
git clone [https://github.com/nasa/](https://github.com/nasa/)<APP_REPO> <app_name>-tmp

# 3. Strip the .git directory to convert it to plain source code
rm -rf <app_name>-tmp/.git

# 4. Move the files into the final destination
# Option A: If the target directory does NOT exist yet:
mv <app_name>-tmp <app_name>

# Option B: If the target directory already exists (e.g., fixing an empty gitlink):
mv <app_name>-tmp/* <app_name>/
mv <app_name>-tmp/.[^.]* <app_name>/ 2>/dev/null # Catches hidden files like .github
rm -rf <app_name>-tmp

# 5. Stage the files normally
git add <app_name>/
```

### The Pre-Commit Check
Always verify that you haven't accidentally staged a gitlink before you commit. Run the following command:

```bash
git diff --cached --raw | grep "^:000000 160000"
```
*Note: If this command returns **any** output, you are about to commit a gitlink. Stop, unstage the directory, remove the nested `.git` folder, and try staging again.*

---

## 2. The EDS MsgId Shim (For Draco-era Apps)

Newer NASA apps (such as **CS, MD,** and **MM**) are tagged as "cFS Draco." These apps define their Message IDs (MIDs) through an EDS topic-ID system that requires the macro `CFE_PLATFORM_CMD_TOPICID_TO_MIDV()`. 

Because NOS3 utilizes an older version of cFE, this macro is not supported out of the box. When adding a Draco-era app, you must implement a shim by manually creating a header file with hardcoded values.

*(Note: Older apps predating Draco-like LC, DS, HK, HS, TO, CI, SCH, CF, FM, and SC-already include hardcoded `_msgids.h` files and do **not** require this step).*

### How to Implement the Shim

1. Create a new file in the app's include directory: `fsw/inc/<app_name>_msgids.h`.
2. Open the app's `fsw/inc/<app_name>_topicids.h` file to find the base topic ID offsets (look for the `DEFAULT_CFE_MISSION_<APP>_XXX_TOPICID` constants).
3. Calculate your hardcoded MIDs using this formula:
   * **CMD MID** = `0x1800` + `topic_id`
   * **TLM MID** = `0x0800` + `topic_id`
4. Add the calculated definitions to your new `_msgids.h` file:

```c
// File: nos3/fsw/apps/<app_name>/fsw/inc/<app_name>_msgids.h

#ifndef <APP_NAME>_MSGIDS_H
#define <APP_NAME>_MSGIDS_H

// Replace 0x18xx and 0x08xx with your calculated values from step 3
#define <APP_NAME>_CMD_MID      0x18xx
#define <APP_NAME>_SEND_HK_MID  0x18xx
#define <APP_NAME>_HK_TLM_MID   0x08xx

#endif
```