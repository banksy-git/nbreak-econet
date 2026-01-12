<script lang="ts">
    import { onMount } from "svelte";
    import { sendWsRequest } from "../../lib/ws";
    import { connectionState } from "../../lib/stores";
    import { type EconetClockSettings } from "../../lib/types";
  
    let clockSettings: EconetClockSettings = {
      mode: "internal",
      internalFrequencyHz: 100000,
      internalDutyCycle: 50,
      invertClock: false,
    };
  
    let loading = true;
    let loadError = "";
    let saving = false;
    let saveStatus: "idle" | "success" | "error" = "idle";
    let saveError = "";
  
    $: isConnected = $connectionState === "connected";
    $: formDisabled = loading || saving || !isConnected;
  
    // Load clock settings when page is shown
    onMount(async () => {
      loading = true;
      loadError = "";
  
      try {
        const res = await sendWsRequest({ type: "get_econet_clock" });
  
        if (res.ok && res.settings) {
          clockSettings = res.settings;
        } else {
          loadError = res.error ?? "Failed to load Econet clock settings";
        }
      } catch {
        loadError = "Connection error while loading Econet clock settings";
      } finally {
        loading = false;
      }
    });
  
    async function saveClockSettings() {
      if (formDisabled) return;
  
      saving = true;
      saveStatus = "idle";
      saveError = "";
  
      try {
        const res = await sendWsRequest({
          type: "save_econet_clock",
          settings: clockSettings,
        });
  
        if (res.ok) {
          saveStatus = "success";
        } else {
          saveStatus = "error";
          saveError = res.error ?? "Failed to save Econet clock settings";
        }
      } catch (e) {
        saveStatus = "error";
        saveError = "Connection error while saving Econet clock settings";
      } finally {
        saving = false;
      }
    }
  
    $: internalPanelDisabled =
      formDisabled || clockSettings.mode !== "internal";
  </script>
  
  {#if loading}
    <p class="text-xs text-gray-500">Loading current clock settings…</p>
  {/if}
  
  {#if loadError}
    <p class="text-xs text-red-600">{loadError}</p>
  {/if}
  
  <section class="bg-white rounded-lg shadow-sm p-4 space-y-4 max-w-md">
    <h2 class="text-sm font-semibold mb-1">Econet Clock Configuration</h2>
  
    <p class="text-xs text-gray-600">
      Configure the Econet clock parameters here.
    </p>
  
    <!-- Mode selection -->
    <div class="space-y-2 text-sm">
      <p class="font-medium text-xs uppercase tracking-wide text-gray-500">
        Clock source
      </p>
  
      <div class="flex items-center gap-4">
        <label class="inline-flex items-center gap-1.5">
          <input
            type="radio"
            name="clockMode"
            value="internal"
            bind:group={clockSettings.mode}
            disabled={formDisabled}
          />
          <span>Internal clock</span>
        </label>
  
        <label class="inline-flex items-center gap-1.5">
          <input
            type="radio"
            name="clockMode"
            value="external"
            bind:group={clockSettings.mode}
            disabled={formDisabled}
          />
          <span>External clock</span>
        </label>
      </div>
  
      <p class="text-xs text-gray-500">
        In <span class="font-medium">external</span> mode, the interface expects a
        clock signal to be provided from outside the device, e.g. from an Econet clock box.
      </p>
    </div>
  
    <!-- Internal clock settings panel -->
    <div
      class="mt-4 border rounded-md p-3 space-y-3 text-sm bg-gray-50"
      class:opacity-50={clockSettings.mode !== "internal" || formDisabled}
    >
      <h3 class="text-xs font-semibold mb-1">
        Internal clock settings
      </h3>
  
      <p class="text-xs text-gray-500">
        Configure the internal clock frequency and duty cycle. The maximum tested
        frequency is <span class="font-semibold">150&nbsp;kHz</span> (150&nbsp;000&nbsp;Hz).
      </p>
  
      <div class="grid grid-cols-1 gap-3">
        <label class="flex flex-col gap-1">
          <span class="text-xs font-medium">Frequency (Hz)</span>
          <input
            type="number"
            min="1"
            max="500000"
            step="1"
            class="border rounded px-2 py-1 text-sm"
            bind:value={clockSettings.internalFrequencyHz}
            disabled={internalPanelDisabled}
          />
          <span class="text-[11px] text-gray-500">
            Recommended value: 100&nbsp;000&nbsp;Hz (100&nbsp;kHz).
          </span>
        </label>
  
        <label class="flex flex-col gap-1">
          <span class="text-xs font-medium">Duty cycle (%)</span>
          <input
            type="number"
            min="1"
            max="99"
            step="1"
            class="border rounded px-2 py-1 text-sm"
            bind:value={clockSettings.internalDutyCycle}
            disabled={internalPanelDisabled}
          />
          <span class="text-[11px] text-gray-500">
            Percentage of each period that the clock signal is high. Maximum recommended value is 50% - greater does not make sense.
          </span>
        </label>
      </div>
    </div>

    <p class="font-medium text-xs uppercase tracking-wide text-gray-500">
      Clock inversion
    </p>

    <p class="text-xs text-gray-600">
      The officially published Econet documentation is inconsistent when describing the polarity of the clock differential lines.
      This lets you invert the clock if the wiring is inverted.
    </p>

    <label class="inline-flex items-center gap-2 cursor-pointer">
      <input
        type="checkbox"
        bind:checked={clockSettings.invertClock}
        disabled={formDisabled}
        class="rounded border-gray-300"
      />
      <span class="text-sm">Invert clock polarity</span>
    </label>
  
    <!-- Save button + status -->
    <div class="pt-2 flex items-center gap-3">
      <button
        class="px-3 py-1.5 text-xs rounded-md bg-sky-600 text-white hover:bg-sky-700 disabled:opacity-50"
        on:click={saveClockSettings}
        disabled={formDisabled}
      >
        {#if saving}
          Saving...
        {:else}
          Save and activate
        {/if}
      </button>
  
      {#if saveStatus === "success"}
        <p class="text-[11px] text-green-600">
          Clock settings saved.
        </p>
      {:else if saveStatus === "error"}
        <p class="text-[11px] text-red-600">
          {saveError}
        </p>
      {/if}
    </div>
  </section>
  