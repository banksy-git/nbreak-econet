<script lang="ts">
  import { sendWsRequest } from "../../lib/ws";
  import { connectionState } from "../../lib/stores";

  type Action = "reboot" | "factory_reset";

  let confirmAction: Action | null = null;
  let running = false;
  let status: "idle" | "success" | "error" = "idle";
  let message = "";

  $: isConnected = $connectionState === "connected";
  $: buttonsDisabled = running || !isConnected;

  function askReboot() {
    status = "idle";
    message = "";
    confirmAction = "reboot";
  }

  function askFactoryReset() {
    status = "idle";
    message = "";
    confirmAction = "factory_reset";
  }

  function closeModal() {
    if (running) return; // don’t close mid-request
    confirmAction = null;
  }

  async function confirm() {
    if (!confirmAction) return;
    running = true;
    status = "idle";
    message = "";

    try {
      const type = confirmAction === "reboot" ? "reboot" : "factory_reset";
      const res = await sendWsRequest({ type });

      if (res.ok) {
        status = "success";
        message =
          confirmAction === "reboot"
            ? "Reboot command sent."
            : "Factory reset command sent.";
      } else {
        status = "error";
        message = res.error ?? "Command failed.";
      }
    } catch {
      status = "error";
      message = "Connection error while sending command.";
    } finally {
      running = false;
      confirmAction = null;
    }
  }
</script>

<section class="bg-white rounded-lg shadow-sm p-4 space-y-3 max-w-md">
  <h2 class="text-sm font-semibold">System</h2>

  {#if status === "success"}
    <p class="text-xs text-emerald-600">{message}</p>
  {:else if status === "error"}
    <p class="text-xs text-red-600">{message}</p>
  {/if}

  <div class="flex gap-2">
    <button
      class="px-3 py-1.5 text-xs rounded-md border hover:bg-gray-50 disabled:opacity-50"
      on:click={askReboot}
      disabled={buttonsDisabled}
    >
      Reboot Device
    </button>

    <button
      class="px-3 py-1.5 text-xs rounded-md border border-red-400 text-red-600 hover:bg-red-50 disabled:opacity-50"
      on:click={askFactoryReset}
      disabled={buttonsDisabled}
    >
      Factory Reset
    </button>
  </div>
</section>

{#if confirmAction}
  <!-- Simple modal -->
  <div class="fixed inset-0 z-40 flex items-center justify-center bg-black/40">
    <div class="bg-white rounded-lg shadow-lg p-4 max-w-sm w-full space-y-3">
      <h3 class="text-sm font-semibold">
        {#if confirmAction === "reboot"}
          Reboot device?
        {:else}
          Factory reset device?
        {/if}
      </h3>

      <p class="text-xs text-gray-600">
        {#if confirmAction === "reboot"}
          Are you sure you want to reboot the device? Active connections may be
          interrupted.
        {:else}
          This will reset the device to factory settings. All configuration
          changes will be lost, WiFi will disconnect and access point enabled. 
          Are you sure?
        {/if}
      </p>

      <div class="flex justify-end gap-2 pt-2">
        <button
          class="px-3 py-1.5 text-xs rounded-md border hover:bg-gray-50 disabled:opacity-50"
          on:click={closeModal}
          disabled={running}
        >
          Cancel
        </button>

        <button
          class="px-3 py-1.5 text-xs rounded-md bg-red-600 text-white hover:bg-red-700 disabled:opacity-50"
          on:click={confirm}
          disabled={running}
        >
          {#if running}
            Sending…
          {:else if confirmAction === "reboot"}
            Yes, reboot
          {:else}
            Yes, factory reset
          {/if}
        </button>
      </div>
    </div>
  </div>
{/if}
