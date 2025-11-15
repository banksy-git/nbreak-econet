<script lang="ts">
  export let label: string;
  export let value: string | number;
  export let highlight = false;
</script>

<div class="flex flex-col">
  <span class="text-xs text-gray-500">{label}</span>
  <span class="font-mono" class:text-red-600={highlight}>
    {value}
  </span>
</div>
