<script lang="ts">
  import { onMount } from "svelte";
  import { sendWsRequest } from "../../lib/ws";
  import { connectionState } from "../../lib/stores";

  type WifiSettings = {
    ssid: string;
    password: string;
  };

  let wifiSettings: WifiSettings = {
    ssid: "",
    password: "",
  };

  let loading = true;        // form disabled while true
  let saving = false;
  let loadError = "";
  let saveStatus: "idle" | "success" | "error" = "idle";
  let saveError = "";

  $: isConnected = $connectionState === "connected";
  $: formDisabled = loading || saving || !isConnected;

  onMount(async () => {
    loading = true;
    loadError = "";

    try {
      const res = await sendWsRequest({ type: "get_wifi" });

      if (res.ok && res.settings) {
        wifiSettings = res.settings;
      } else {
        loadError = res.error ?? "Failed to load WiFi settings";
      }
    } catch (e) {
      loadError = "Connection error while loading WiFi settings";
    } finally {
      loading = false;
    }
  });

  async function saveWifi() {
    if (formDisabled) return;

    saving = true;
    saveStatus = "idle";
    saveError = "";

    try {
      const res = await sendWsRequest({
        type: "save_wifi",
        settings: wifiSettings,
      });

      if (res.ok) {
        saveStatus = "success";
      } else {
        saveStatus = "error";
        saveError = res.error ?? "Failed to save WiFi settings";
      }
    } catch (e) {
      saveStatus = "error";
      saveError = "Connection error while saving";
    } finally {
      saving = false;
    }
  }
</script>

<section class="bg-white rounded-lg shadow-sm p-4 space-y-4 max-w-md">
  <h2 class="text-sm font-semibold mb-1">WiFi Network Settings</h2>

  {#if loading}
    <p class="text-xs text-gray-500">Loading current settings…</p>
  {/if}

  {#if loadError}
    <p class="text-xs text-red-600">{loadError}</p>
  {/if}

  <div class="space-y-2 text-sm opacity-{formDisabled ? 50 : 100}">
    <label class="block">
      <span class="text-xs text-gray-600">WiFi network name (SSID)</span>
      <input
        class="mt-1 block w-full rounded-md border-gray-300 text-sm shadow-sm focus:border-sky-500 focus:ring-sky-500"
        type="text"
        bind:value={wifiSettings.ssid}
        disabled={formDisabled}
      />
    </label>

    <label class="block">
      <span class="text-xs text-gray-600">Password</span>
      <input
        class="mt-1 block w-full rounded-md border-gray-300 text-sm shadow-sm focus:border-sky-500 focus:ring-sky-500"
        type="password"
        bind:value={wifiSettings.password}
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
      on:click={saveWifi}
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
