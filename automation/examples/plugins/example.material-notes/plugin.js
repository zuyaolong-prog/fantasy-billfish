'use strict';
eagle.onPluginCreate(() => { console.log('Material notes plugin ready'); });
billfish.registerCommand('annotate', async ({item_ids, note_prefix = '素材: '}) => {
  if (item_ids.some(id => !/^[1-9][0-9]{0,18}$/.test(id))) throw new Error('item_ids must contain positive decimal string IDs');
  const changed = [];
  billfish.progress(0,item_ids.length,'准备填写素材备注');
  for (const id of item_ids) {
    const item = await eagle.item.getById(id);
    item.annotation = note_prefix + item.name + (item.ext ? '.' + item.ext : '');
    await item.save();
    changed.push(id);
    billfish.progress(changed.length,item_ids.length,'已填写 ' + item.name);
  }
  return {updated_item_ids:changed,count:changed.length};
});
eagle.onPluginBeforeExit(() => { console.log('Material notes plugin stopped'); });
