<script lang="ts">
  import { onMount } from "svelte";
  import { sendWsRequest } from "../../lib/ws";
  import { connectionState } from "../../lib/stores";
  import EditableTable, {
    type ColumnDef,
  } from "../layout/EditableTable.svelte";

  import type { EconetSettings, TrunkRow } from "../../lib/types";

  let econetSettings: EconetSettings = {
    econetStations: [],
    aunStations: [],
    trunks: [],
    trunkOurNet: 88,
  };

  let loading = true;
  let loadError = "";
  let saving = false;
  let saveStatus: "idle" | "success" | "error" = "idle";
  let saveError = "";

  $: isConnected = $connectionState === "connected";
  $: formDisabled = loading || saving || !isConnected;

  const uplinkColumns: ColumnDef<TrunkRow>[] = [
    { label: "Remote IP Address", key: "remote_ip", type: "string" },
    { label: "Remote UDP port", key: "udp_port", type: "number" },
    { label: "Encryption key", key: "aes_key", type: "string" },
  ];

  function uplinkOnChange(newRows: TrunkRow[]) {
    econetSettings.trunks = newRows;
  }

  // Load uplink settings when page is shown
  onMount(async () => {
    loading = true;
    loadError = "";

    try {
      const res = await sendWsRequest({ type: "get_econet_uplinks" });

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
        type: "save_econet_uplinks",
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

{#if loading}
  <p class="text-xs text-gray-500">Loading current settings…</p>
{/if}

{#if loadError}
  <p class="text-xs text-red-600">{loadError}</p>
{/if}

<section class="bg-white rounded-lg shadow-sm p-4 space-y-4 max-w-md">
  <h2 class="text-sm font-semibold mb-1">Econet Uplinks</h2>

  <p class="text-xs text-gray-600">
    Uplinks are used to connect the Econet interface to a wide-area Econet
    network using the bridge-over-IP protocol created by the PiEconetBridge
    project.
  </p>

  <div class="space-y-4 text-sm opacity-{formDisabled ? 50 : 100}">
    <!-- Local network number -->
    <div class="space-y-2">
      <label class="flex flex-col gap-1">
        <span class="text-xs font-medium text-gray-700"
          >Local network number</span
        >
        <input
          type="number"
          min="1"
          max="255"
          bind:value={econetSettings.trunkOurNet}
          disabled={formDisabled}
          class="px-2 py-1 text-sm border rounded-md disabled:bg-gray-100"
          placeholder="88"
        />
      </label>
      <p class="text-xs text-gray-500">
        The network number that identifies this bridge on the Econet network
        (1-255). Default is 88 if not specified.
      </p>
    </div>

    <!-- Trunk uplinks table -->
    <div class="space-y-2">
      <p class="text-xs font-medium text-gray-700">Trunk connections</p>
      <EditableTable
        columns={uplinkColumns}
        rows={econetSettings?.trunks || []}
        onChange={uplinkOnChange}
      />
    </div>

    <div class="flex items-center gap-2">
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

      {#if saveStatus === "success"}
        <span class="text-xs text-emerald-600">Saved successfully.</span>
      {:else if saveStatus === "error"}
        <span class="text-xs text-red-600">{saveError}</span>
      {/if}
    </div>
  </div>
</section>
