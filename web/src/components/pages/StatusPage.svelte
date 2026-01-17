<script lang="ts">
  import { econetStats, aunbridgeStats } from "../../lib/stores";
  import { type AunbridgeStats, type EconetStats } from "../../lib/types";
  import StatItem from "../ui/StatItem.svelte";

  type FieldSpec<T> = {
    key: keyof T;
    label: string;
    warn?: boolean;
  };

  // Define fields for Econet
  const econetFields: FieldSpec<EconetStats>[] = [
    { key: "rx_frame_count", label: "RX Frames" },
    { key: "rx_crc_fail_count", label: "RX CRC Fail", warn: true },
    { key: "rx_short_frame_count", label: "RX Short Frame", warn: true },
    { key: "rx_abort_count", label: "RX Abort", warn: true },
    { key: "rx_oversize_count", label: "RX Oversize", warn: true },
    { key: "rx_ack_count", label: "RX ACK" },
    { key: "rx_nack_count", label: "RX NACK", warn: true },
    { key: "tx_frame_count", label: "TX Frames" },
    { key: "tx_ack_count", label: "TX ACK" },
  ];

  // Fields for AUN
  const aunFields: FieldSpec<AunbridgeStats>[] = [
    { key: "tx_count", label: "TX Count" },
    { key: "tx_retry_count", label: "TX Retry", warn: true },
    { key: "tx_abort_count", label: "TX Abort", warn: true },
    { key: "tx_error_count", label: "TX Error", warn: true },
    { key: "tx_ack_count", label: "TX Ack" },
    { key: "tx_nack_count", label: "TX Nack", warn: true },
    { key: "tx_bridge_control", label: "TX Bridge Control" },
    { key: "tx_broadcast_count", label: "TX Broadcast" },
    { key: "rx_imm_count", label: "RX Immediate" },
    { key: "rx_data_count", label: "RX Data" },
    { key: "rx_ack_count", label: "RX Ack" },
    { key: "rx_nack_count", label: "RX Nack", warn: true },
    { key: "rx_unknown_count", label: "RX Unknown" },
    { key: "rx_bridge_control", label: "RX Bridge Control" },
    { key: "rx_broadcast_count", label: "RX Broadcast" },
  ];
</script>

<section class="bg-white rounded-lg shadow-sm p-4">
  <h2 class="text-sm font-semibold mb-3">Econet Stats</h2>

  <div class="grid grid-cols-2 sm:grid-cols-3 gap-3 text-sm">
    {#each econetFields as field}
      <StatItem
        label={field.label}
        value={$econetStats[field.key]}
        highlight={field.warn ? $econetStats[field.key] > 0 : false}
      />
    {/each}
  </div>
</section>

<section class="bg-white rounded-lg shadow-sm p-4">
  <h2 class="text-sm font-semibold mb-3">AUN Bridge Stats</h2>

  <div class="grid grid-cols-2 sm:grid-cols-3 gap-3 text-sm">
    {#each aunFields as field}
      <StatItem
        label={field.label}
        value={$aunbridgeStats[field.key]}
        highlight={field.warn ? $aunbridgeStats[field.key] > 0 : false}
      />
    {/each}
  </div>
</section>
