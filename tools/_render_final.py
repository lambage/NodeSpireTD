import bpy

scene = bpy.context.scene
scene.render.filepath = "E:/projects/NodeSpireTD/_to_delete/render_final.png"
scene.render.image_settings.file_format = 'PNG'
bpy.ops.render.render(write_still=True)
print("rendered", scene.render.filepath)
