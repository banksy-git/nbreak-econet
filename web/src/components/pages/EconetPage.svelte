<script lang="ts">
  import { onMount } from "svelte";
  import { sendWsRequest } from "../../lib/ws";
  import { connectionState } from "../../lib/stores";

  type EconetSettings = {
    remote_station_id: number;
    this_station_id: number;
    server_ip: string;
    server_port: number;
  };

  let econetSettings: EconetSettings = {
    remote_station_id: 0,
    this_station_id: 0,
    server_ip: "",
    server_port: 0
  };

  let loading = true;
  let loadError = "";
  let saving = false;
  let saveStatus: "idle" | "success" | "error" = "idle";
  let saveError = "";

  $: isConnected = $connectionState === "connected";
  $: formDisabled = loading || saving || !isConnected;

  // Load econet settings when page is shown
  onMount(async () => {
    loading = true;
    loadError = "";

    try {
      const res = await sendWsRequest({ type: "get_econet" });

      if (res.ok && res.settings) {
        econetSettings = res.settings;
      } else {
        loadError = res.error ?? "Failed to load Econet settings";
      }
    } catch {
      loadError = "Connection error while loading Econet settings";
    } finally {
      loading = false;
    }
  });

  async function saveEconet() {
    if (formDisabled) return;

    saving = true;
    saveStatus = "idle";
    saveError = "";

    try {
      const res = await sendWsRequest({
        type: "save_econet",
        settings: econetSettings,
      });

      if (res.ok) {
        saveStatus = "success";
      } else {
        saveStatus = "error";
        saveError = res.error ?? "Failed to save Econet settings";
      }
    } catch (e) {
      saveStatus = "error";
      saveError = "Connection error while saving Econet settings";
    } finally {
      saving = false;
    }
  }
</script>

<section class="bg-white rounded-lg shadow-sm p-4 space-y-4 max-w-md">
  <h2 class="text-sm font-semibold mb-1">Econet settings</h2>

  {#if loading}
    <p class="text-xs text-gray-500">Loading current settings…</p>
  {/if}

  {#if loadError}
    <p class="text-xs text-red-600">{loadError}</p>
  {/if}

  <div class="space-y-2 text-sm opacity-{formDisabled ? 50 : 100}">
    <label class="block">
      <span class="text-xs text-gray-600">
        Remote Station ID (ID of connected BBC Micro)
      </span>
      <input
        class="mt-1 block w-full rounded-md border-gray-300 text-sm shadow-sm focus:border-sky-500 focus:ring-sky-500"
        type="number" step="1" min="1" max="254"
        bind:value={econetSettings.remote_station_id}
        disabled={formDisabled}
      />
    </label>

    <label class="block">
      <span class="text-xs text-gray-600">This Station ID</span>
      <input
        class="mt-1 block w-full rounded-md border-gray-300 text-sm shadow-sm focus:border-sky-500 focus:ring-sky-500"
        type="number" step="1" min="1" max="254"
        bind:value={econetSettings.this_station_id}
        disabled={formDisabled}
      />
    </label>

    <label class="block">
      <span class="text-xs text-gray-600">Server IP</span>
      <input
        class="mt-1 block w-full rounded-md border-gray-300 text-sm shadow-sm focus:border-sky-500 focus:ring-sky-500"
        type="text"
        bind:value={econetSettings.server_ip}
        disabled={formDisabled}
      />
    </label>

    <label class="block">
      <span class="text-xs text-gray-600">Server port</span>
      <input
        class="mt-1 block w-full rounded-md border-gray-300 text-sm shadow-sm focus:border-sky-500 focus:ring-sky-500"
        type="number" step="1" min="1" max="65535"
        bind:value={econetSettings.server_port}
        disabled={formDisabled}
      />
    </label>
  </div>

  <div class="flex items-center justify-between pt-2 gap-2">
    {#if saveStatus === "success"}
      <span class="text-xs text-emerald-600">Saved successfully.</span>
    {:else if saveStatus === "error"}
      <span class="text-xs text-red-600">{saveError}</span>
    {/if}

    <button
      class="px-3 py-1.5 text-xs rounded-md bg-sky-600 text-white hover:bg-sky-700 disabled:opacity-50"
      on:click={saveEconet}
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