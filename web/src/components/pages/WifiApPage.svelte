<script lang="ts">
  import { onMount } from "svelte";
  import { sendWsRequest } from "../../lib/ws";
  import { connectionState } from "../../lib/stores";

  type WifiApSettings = {
    ssid: string;
    password: string;
    enabled: boolean;
  };

  let wifiApSettings: WifiApSettings = {
    ssid: "",
    password: "",
    enabled: true,
  };

  let loading = true;
  let loadError = "";
  let saving = false;
  let saveStatus: "idle" | "success" | "error" = "idle";
  let saveError = "";

  $: isConnected = $connectionState === "connected";
  $: formDisabled = loading || saving || !isConnected;

  onMount(async () => {
    loading = true;
    loadError = "";

    try {
      const res = await sendWsRequest({ type: "get_wifi_ap" });

      if (res.ok && res.settings) {
        wifiApSettings = res.settings;
      } else {
        loadError = res.error ?? "Failed to load access point settings";
      }
    } catch {
      loadError = "Connection error while loading access point settings";
    } finally {
      loading = false;
    }
  });

  async function saveWifiAp() {
    if (formDisabled) return;

    saving = true;
    saveStatus = "idle";
    saveError = "";

    try {
      const res = await sendWsRequest({
        type: "save_wifi_ap",
        settings: wifiApSettings,
      });

      if (res.ok) {
        saveStatus = "success";
      } else {
        saveStatus = "error";
        saveError = res.error ?? "Failed to save access point settings";
      }
    } catch {
      saveStatus = "error";
      saveError = "Connection error while saving access point settings";
    } finally {
      saving = false;
    }
  }
</script>

<section class="bg-white rounded-lg shadow-sm p-4 space-y-4 max-w-md">
  <h2 class="text-sm font-semibold mb-1">WiFi Access point settings</h2>

  {#if loading}
    <p class="text-xs text-gray-500">Loading current access point settings…</p>
  {/if}

  {#if loadError}
    <p class="text-xs text-red-600">{loadError}</p>
  {/if}

  <div class="space-y-2 text-sm opacity-{formDisabled ? 50 : 100}">
    <label class="block">
      <span class="text-xs text-gray-600">Access Point Name (SSID)</span>
      <input
        class="mt-1 block w-full rounded-md border-gray-300 text-sm shadow-sm focus:border-sky-500 focus:ring-sky-500"
        type="text"
        bind:value={wifiApSettings.ssid}
        disabled={formDisabled}
      />
    </label>

    <label class="block">
      <span class="text-xs text-gray-600">Password</span>
      <input
        class="mt-1 block w-full rounded-md border-gray-300 text-sm shadow-sm focus:border-sky-500 focus:ring-sky-500"
        type="password"
        bind:value={wifiApSettings.password}
        disabled={formDisabled}
      />
    </label>

    <label class="block">
      <span class="text-xs text-gray-600">Enable access point</span>
      <input
        class="mt-1"
        type="checkbox"
        bind:checked={wifiApSettings.enabled}
        disabled={formDisabled}
      />
    </label>
  </div>

  <div class="flex items-center justify-between gap-2 pt-2">
    {#if saveStatus === "success"}
      <span class="text-xs text-emerald-600">Saved successfully.</span>
    {:else if saveStatus === "error"}
      <span class="text-xs text-red-600">{saveError}</span>
    {/if}

    <button
      class="px-3 py-1.5 text-xs rounded-md bg-sky-600 text-white hover:bg-sky-700 disabled:opacity-50"
      on:click={saveWifiAp}
      disabled={formDisabled}
    >
      {#if saving}
        Saving...
      {:else}
        Save and activate
      {/if}
    </button>
  </div>
</section>
