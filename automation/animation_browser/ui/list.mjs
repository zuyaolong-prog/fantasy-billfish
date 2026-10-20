export const compatibilityNames = {full:'完全兼容',partial:'部分兼容',incompatible:'不兼容'};
export function visibleRows(rows,settings) {
  const query = (settings.search || '').trim().toLocaleLowerCase();
  return rows.filter(row => (!query || `${row.name} ${row.relativePath || ''}`.toLocaleLowerCase().includes(query))
    && (!settings.compatibility || row.compatibility === settings.compatibility)
    && (!settings.format || row.format?.toLowerCase() === settings.format.toLowerCase())
    && (!settings.folder || row.folder === settings.folder)
    && (!settings.minDuration || row.duration >= Number(settings.minDuration))
    && (!settings.maxDuration || row.duration <= Number(settings.maxDuration)))
    .sort((a,b) => settings.sort === 'duration' ? a.duration-b.duration || a.name.localeCompare(b.name) : settings.sort === 'folder' ? (a.folder || '').localeCompare(b.folder || '') || a.name.localeCompare(b.name) : a.name.localeCompare(b.name,undefined,{numeric:true}));
}
export function seconds(value) { return Number.isFinite(value) ? `${value.toFixed(2)} s` : '-'; }
export function fpsLabel(value) { return Number.isFinite(value) && value > 0 ? `${Number(value.toFixed(2))} FPS` : 'FPS 未知'; }
