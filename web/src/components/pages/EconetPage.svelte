<script lang="ts">
  import { onMount } from "svelte";
  import { sendWsRequest } from "../../lib/ws";
  import { connectionState } from "../../lib/stores";
  import EditableTable, {
    type ColumnDef,
  } from "../layout/EditableTable.svelte";

  import type {EconetSettings, AUNRow, ECSRow} from "../../lib/types"

  let econetSettings: EconetSettings = {
    econet: {
      localStations: [],
      remoteStations: [],
    },
  };

  let loading = true;
  let loadError = "";
  let saving = false;
  let saveStatus: "idle" | "success" | "error" = "idle";
  let saveError = "";

  $: isConnected = $connectionState === "connected";
  $: formDisabled = loading || saving || !isConnected;

  const ecsColumns: ColumnDef<ECSRow>[] = [
    { label: "Station ID", key: "station_id", type: "number" },
    { label: "Local UDP port", key: "udp_port", type: "number" },
  ];

  function ecsOnChange(newRows: ECSRow[]) {
    if (!econetSettings.econet) {
      econetSettings.econet = { localStations: newRows };
    } else {
      econetSettings.econet.localStations = newRows;
    }
  }

  const aunColumns: ColumnDef<AUNRow>[] = [
    { label: "Remote IP Address", key: "remote_ip", type: "string" },
    { label: "Remote UDP port", key: "udp_port", type: "number" },
    { label: "Station ID", key: "station_id", type: "number" },
  ];

  function aunOnChange(newRows: AUNRow[]) {
    if (!econetSettings.econet) {
      econetSettings.econet = { remoteStations: newRows };
    } else {
      econetSettings.econet.remoteStations = newRows;
    }
  }

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

{#if loading}
  <p class="text-xs text-gray-500">Loading current settings…</p>
{/if}

{#if loadError}
  <p class="text-xs text-red-600">{loadError}</p>
{/if}

<section class="bg-white rounded-lg shadow-sm p-4 space-y-4 max-w-md">
  <h2 class="text-sm font-semibold mb-1">Econet Stations List</h2>

  <p>
    Add stations to this list that are physically present on the Econet network.
    They will be accessable from the IP network at the given UDP port.
  </p>

  <div class="space-y-2 text-sm opacity-{formDisabled ? 50 : 100}">
    <EditableTable
      columns={ecsColumns}
      rows={econetSettings.econet?.localStations || []}
      onChange={ecsOnChange}
    />

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

<section class="bg-white rounded-lg shadow-sm p-4 space-y-4 max-w-md">
  <h2 class="text-sm font-semibold mb-1">AUN Stations List</h2>

  <p>
    Add AUN addresses to this list. They will be made accessible on the Econet
    network at the station address given.
  </p>

  <div class="space-y-2 text-sm opacity-{formDisabled ? 50 : 100}">
    <EditableTable
      columns={aunColumns}
      rows={econetSettings.econet?.remoteStations || []}
      onChange={aunOnChange}
    />

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
