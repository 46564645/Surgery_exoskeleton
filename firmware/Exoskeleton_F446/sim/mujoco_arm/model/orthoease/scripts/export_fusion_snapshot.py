# Run inside FusionLocalMCP. Read-only export: does not move or modify the assembly.
import json,datetime,gzip,os
for doc in app.documents:
 if doc.name=='final model':doc.activate();break
design=adsk.fusion.Design.cast(app.activeProduct);root=design.rootComponent
assert app.activeDocument.name=='final model'
assert all(abs(j.jointMotion.rotationValue)<1e-8 for j in root.asBuiltJoints)
out='/Users/mzc/Desktop/orthoease/MuJoCo_final_model/metadata/fusion_snapshot.json.gz'
parts=[]
for o in root.allOccurrences:
 if not o.isVisible:continue
 for b in o.component.meshBodies:
  m=b.mesh
  parts.append({'path':o.fullPathName,'name':b.name,'kind':'mesh','transform_cm':list(o.transform2.asArray()),'vertices_cm':list(m.nodeCoordinatesAsDouble),'triangles':list(m.triangleNodeIndices)})
 for b in o.component.bRepBodies:
  calc=b.meshManager.createMeshCalculator();calc.surfaceTolerance=0.005
  m=calc.calculate()
  parts.append({'path':o.fullPathName,'name':b.name,'kind':'brep_tessellated_0.05mm','transform_cm':list(o.transform2.asArray()),'vertices_cm':list(m.nodeCoordinatesAsDouble),'triangles':list(m.nodeIndices)})
data={'document':app.activeDocument.name,'date':datetime.datetime.now().isoformat(),'unit':'cm','parts':parts,'groups':[{'name':g.name,'members':[o.fullPathName for o in g.occurrences]} for g in root.rigidGroups],'joints':[{'name':j.name,'transform_cm':list(j.transform.asArray()),'axis':j.jointMotion.rotationAxisVector.asArray(),'limits_rad':[j.jointMotion.rotationLimits.minimumValue,j.jointMotion.rotationLimits.maximumValue],'angle_rad':j.jointMotion.rotationValue} for j in root.asBuiltJoints]}
with gzip.open(out,'wt',encoding='utf-8') as f:json.dump(data,f,ensure_ascii=False)
result={'out':out,'parts':[{'path':p['path'],'triangles':len(p['triangles'])//3} for p in parts]}
