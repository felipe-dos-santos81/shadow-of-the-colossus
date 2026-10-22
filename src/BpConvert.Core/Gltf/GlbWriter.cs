using System.Buffers.Binary;
using System.Numerics;
using System.Text.Json;
using BpConvert.Core.Anim;
using BpConvert.Core.Model;

namespace BpConvert.Core.Gltf;

/// <summary>
/// Minimal in-house writer for glTF 2.0 binary (.glb) files: one JSON chunk plus one BIN chunk.
/// No axis conversion (game and glTF are both Y-up), no external dependencies. The output is
/// accepted by any conformant loader; the tests prove it by reloading it with SharpGLTF.
/// </summary>
/// <remarks>
/// <para>
/// A <see cref="ModelData.Skeleton"/> additionally gives the joint nodes their bind-pose local
/// transforms (derived from the inverse bind matrices — see <c>BindPoseLocalMatrices</c>), so the
/// skinned mesh renders undeformed at bind pose. Models without a skeleton are written exactly as
/// they were before that support existed (no node <c>matrix</c> properties at all).
/// </para>
/// <para>
/// <see cref="ModelData.Animations"/> adds one glTF animation per clip (rotation per track, root
/// translation, LINEAR, times <c>i / fps</c>). Only the joint nodes targeted by a channel switch
/// from bind-pose <c>matrix</c> to decomposed TRS; every other node keeps its <c>matrix</c>, and a
/// model with no animations is written exactly as before (no <c>animations</c> array).
/// </para>
/// </remarks>
public static class GlbWriter
{
    /// <summary>
    /// Writes <paramref name="model"/> as <paramref name="glbName"/>.glb under <paramref name="outDir"/>
    /// (created when missing; a ".glb" suffix is appended unless the name already has one).
    /// </summary>
    /// <param name="textureUriMap">Source .ctxr relative path -> output .png relative URI (from the .glb's directory).</param>
    public static GlbWriteResult Write(
        ModelData model, string outDir, string glbName, IReadOnlyDictionary<string, string> textureUriMap)
    {
        ArgumentNullException.ThrowIfNull(model);
        ArgumentNullException.ThrowIfNull(textureUriMap);
        ArgumentException.ThrowIfNullOrWhiteSpace(outDir);
        ArgumentException.ThrowIfNullOrWhiteSpace(glbName);

        Directory.CreateDirectory(outDir);
        var fileName = glbName.EndsWith(".glb", StringComparison.OrdinalIgnoreCase) ? glbName : glbName + ".glb";
        var glbPath = Path.Combine(outDir, fileName);

        var glb = new Builder(textureUriMap).Build(model);

        File.WriteAllBytes(glbPath, glb);
        return new GlbWriteResult(glbPath, glb.LongLength);
    }

    private sealed class Builder
    {
        private const uint GlbMagic = 0x46546C67;      // "glTF"
        private const uint JsonChunkType = 0x4E4F534A; // "JSON"
        private const uint BinChunkType = 0x004E4942;  // "BIN\0"

        private const int ComponentUShort = 5123;
        private const int ComponentUInt = 5125;
        private const int ComponentFloat = 5126;
        private const int TargetArrayBuffer = 34962;
        private const int TargetElementArrayBuffer = 34963;

        private readonly IReadOnlyDictionary<string, string> _textureUriMap;

        // BIN chunk contents.
        private readonly MemoryStream _bin = new();
        private readonly List<BufferViewInfo> _views = new();
        private readonly List<AccessorInfo> _accessors = new();

        // Images/textures, deduplicated by output uri (one image + one texture per uri).
        private readonly List<string> _imageUris = new();
        private readonly Dictionary<string, int> _textureIndexByUri = new(StringComparer.Ordinal);
        private bool _usesSpecularExtension;

        // Data collected while building the BIN chunk, consumed while writing JSON.
        private List<SkeletonJoint> _joints = new();
        private List<int>[] _jointChildren = Array.Empty<List<int>>();
        private List<int> _jointRoots = new();
        private float[] _jointLocalMatrices = Array.Empty<float>();
        private int _ibmAccessor = -1;
        private string _skinName = string.Empty;
        private List<PrimitiveInfo> _primitives = new();
        private List<MaterialData> _materials = new();
        private List<MaterialTextures> _materialTextures = new();

        // Animations: one per clip, and the joint nodes whose bind-pose `matrix` is replaced by
        // decomposed TRS so their channels can drive it (docs/notes/aqua-core-api.md §11.2).
        private List<AnimationInfo> _animations = new();
        private bool[] _animatedJoints = Array.Empty<bool>();
        private (Vector3 Translation, Quaternion Rotation, Vector3 Scale)[] _jointBindTrs =
            Array.Empty<(Vector3, Quaternion, Vector3)>();

        public Builder(IReadOnlyDictionary<string, string> textureUriMap) => _textureUriMap = textureUriMap;

        public byte[] Build(ModelData model)
        {
            BuildSkeleton(model.Skeleton);
            foreach (var mesh in model.Meshes) _primitives.Add(AddMesh(mesh, _joints.Count, model.Materials.Count));
            _materials = model.Materials;
            foreach (var material in _materials)
            {
                var textures = new MaterialTextures(
                    ResolveTexture(material.BaseColorTexture),
                    ResolveTexture(material.NormalTexture),
                    ResolveTexture(material.SpecularTexture),
                    ResolveTexture(material.EmissiveTexture));
                if (textures.Specular >= 0) _usesSpecularExtension = true;
                _materialTextures.Add(textures);
            }

            BuildAnimations(model.Animations);

            var json = WriteJson();
            return Pack(json, _bin.ToArray());
        }

        /// <summary>Creates joint nodes (indices 0..J-1) and the inverse bind matrices accessor.</summary>
        private void BuildSkeleton(SkeletonData? skeleton)
        {
            if (skeleton == null || skeleton.Joints.Count == 0) return;

            _joints = skeleton.Joints;
            _skinName = skeleton.Name;
            _jointChildren = new List<int>[_joints.Count];
            for (int i = 0; i < _jointChildren.Length; i++) _jointChildren[i] = new List<int>();

            var ibm = new float[_joints.Count * 16];
            for (int i = 0; i < _joints.Count; i++)
            {
                var matrix = _joints[i].InverseBindMatrix16;
                if (matrix is not { Length: 16 })
                    throw new InvalidOperationException($"Joint '{_joints[i].Name}': InverseBindMatrix16 must have 16 elements.");

                Array.Copy(matrix, 0, ibm, i * 16, 16);

                var parent = _joints[i].ParentIndex;
                if (parent < 0) _jointRoots.Add(i);
                else if (parent < _joints.Count) _jointChildren[parent].Add(i);
                else throw new InvalidOperationException($"Joint '{_joints[i].Name}': parent index {parent} is out of range.");
            }

            _ibmAccessor = AddAccessor(FloatBytes(ibm), null, ComponentFloat, "MAT4", _joints.Count, null, null);
            _jointLocalMatrices = BindPoseLocalMatrices(_joints);
        }

        /// <summary>
        /// Adds one glTF animation per clip: a rotation channel per track, a translation channel
        /// for the root series, LINEAR samplers with times <c>i / fps</c>. Every targeted joint
        /// node switches from bind-pose <c>matrix</c> to decomposed TRS (animation channels cannot
        /// drive a <c>matrix</c> node); all others keep their <c>matrix</c>.
        /// </summary>
        /// <exception cref="InvalidDataException">
        /// A clip has an unmapped/out-of-range track, a malformed root series, a root series but
        /// no joint named exactly <c>Bone_root</c>, duplicate channels on one joint/path, zero
        /// channels, or the model has no skeleton to target.
        /// </exception>
        private void BuildAnimations(List<AnimClip> clips)
        {
            if (clips.Count == 0) return;
            if (_joints.Count == 0)
                throw new InvalidDataException($"Clip '{clips[0].Name}': the model has no skeleton to target.");

            var jointByName = new Dictionary<string, int>(_joints.Count, StringComparer.Ordinal);
            for (var joint = 0; joint < _joints.Count; joint++)
                jointByName.TryAdd(_joints[joint].Name, joint);

            _animatedJoints = new bool[_joints.Count];
            _jointBindTrs = new (Vector3, Quaternion, Vector3)[_joints.Count];

            foreach (var clip in clips)
            {
                if (clip.Frames < 1)
                    throw new InvalidDataException($"Clip '{clip.Name}': frame count {clip.Frames} must be positive.");
                if (!(clip.Fps > 0))
                    throw new InvalidDataException($"Clip '{clip.Name}': fps {clip.Fps} must be positive.");
                if (clip.Root.Count > 0 && clip.Root.Count != clip.Frames)
                    throw new InvalidDataException(
                        $"Clip '{clip.Name}': root series has {clip.Root.Count} entries, expected {clip.Frames} (one per frame).");

                var rootJoint = -1;
                if (clip.Root.Count > 0 && !jointByName.TryGetValue("Bone_root", out rootJoint))
                    throw new InvalidDataException(
                        $"Clip '{clip.Name}': carries a root series but the skeleton has no joint named 'Bone_root'.");

                var channels = new List<ChannelInfo>();
                var channelTargets = new HashSet<(int Node, string Path)>();
                var samplers = new List<SamplerInfo>();

                void AddChannel(int node, string path, int sampler)
                {
                    if (!channelTargets.Add((node, path)))
                        throw new InvalidDataException(
                            $"Clip '{clip.Name}': duplicate {path} channel for joint '{_joints[node].Name}' (index {node}).");
                    channels.Add(new ChannelInfo(node, path, sampler));
                }

                foreach (var track in clip.Tracks)
                {
                    if (track.JointIndex < 0)
                        throw new InvalidDataException(
                            $"Clip '{clip.Name}' track {track.Index}: JointIndex is -1 (unmapped); refusing to drop the channel.");
                    if (track.JointIndex >= _joints.Count)
                        throw new InvalidDataException(
                            $"Clip '{clip.Name}' track {track.Index}: JointIndex {track.JointIndex} is out of range (joints: {_joints.Count}).");
                    if (track.Keys.Count == 0)
                        throw new InvalidDataException($"Clip '{clip.Name}' track {track.Index}: no keys.");

                    _animatedJoints[track.JointIndex] = true;

                    // Statics are a single key at t = 0. Keyed tracks emit one key per engine
                    // frame: type 0xD may carry frames+1 keys, but the surplus key at index
                    // `frames` is a raw-zero sentinel outside the engine's sampled range
                    // [0, frames-1] — not a loop-closure key — and the engine's duration is
                    // (frames-1)/fps, so it is not emitted.
                    var count = track.IsStatic ? 1 : Math.Min(track.Keys.Count, clip.Frames);
                    var times = new float[count];
                    var values = new float[count * 4];
                    for (var key = 0; key < count; key++)
                    {
                        times[key] = (float)(key / clip.Fps);
                        var raw = track.Keys[key];
                        var bridged = AnimBridge.ToGlb(new Vector3(raw.X, raw.Y, raw.Z), raw.W);
                        var rotation = Quaternion.Normalize(bridged);
                        values[key * 4] = rotation.X;
                        values[key * 4 + 1] = rotation.Y;
                        values[key * 4 + 2] = rotation.Z;
                        values[key * 4 + 3] = rotation.W;
                    }

                    samplers.Add(AddSampler(times, values, "VEC4"));
                    AddChannel(track.JointIndex, "rotation", samplers.Count - 1);
                }

                if (clip.Root.Count > 0)
                {
                    _animatedJoints[rootJoint] = true;

                    // Root translation, in place: x/z are zeroed (the controller owns horizontal
                    // movement); y is the raw series value (ground height + bob).
                    var times = new float[clip.Frames];
                    var values = new float[clip.Frames * 3];
                    for (var key = 0; key < clip.Frames; key++)
                    {
                        times[key] = (float)(key / clip.Fps);
                        values[key * 3] = 0f;
                        values[key * 3 + 1] = clip.Root[key].Y;
                        values[key * 3 + 2] = 0f;
                    }

                    samplers.Add(AddSampler(times, values, "VEC3"));
                    AddChannel(rootJoint, "translation", samplers.Count - 1);
                }

                if (channels.Count == 0)
                    throw new InvalidDataException(
                        $"Clip '{clip.Name}': produced no animation channels (an empty glTF animation is non-conformant).");

                _animations.Add(new AnimationInfo(clip.Name, channels, samplers));
            }

            // Animated nodes need TRS: decompose their bind-pose local transform once. This is the
            // mechanical `matrix` → translation/rotation/scale substitution of notes §11.2.
            for (var joint = 0; joint < _joints.Count; joint++)
            {
                if (!_animatedJoints[joint]) continue;

                var local = ToMatrix(_jointLocalMatrices, joint * 16);
                if (!Matrix4x4.Decompose(local, out var scale, out var rotation, out var translation))
                    throw new InvalidOperationException(
                        $"Joint '{_joints[joint].Name}': bind-pose local matrix cannot be decomposed into TRS.");
                _jointBindTrs[joint] = (translation, Quaternion.Normalize(rotation), scale);
            }
        }

        /// <summary>Adds an animation input/output accessor pair and returns its sampler entry.</summary>
        private SamplerInfo AddSampler(float[] times, float[] values, string outputType)
        {
            var input = AddAccessor(FloatBytes(times), null, ComponentFloat, "SCALAR", times.Length,
                new[] { times[0] }, new[] { times[^1] });
            var output = AddAccessor(FloatBytes(values), null, ComponentFloat, outputType, times.Length, null, null);
            return new SamplerInfo(input, output);
        }

        /// <summary>
        /// The bind-pose local transform of every joint, for its node's <c>matrix</c>: glTF feeds a
        /// skinned vertex through <c>globalTransform(joint) × inverseBindMatrix</c>, so a joint
        /// renders undeformed at bind pose when its world transform is <c>inverse(IBM)</c>; its
        /// local transform is that world transform relative to its parent's (roots are absolute).
        /// </summary>
        /// <remarks>
        /// The DTO's 16 floats arrive in glTF's element order — column-major, i.e. the row-vector
        /// matrix's rows in order (adapter notes §11) — and this type emits the same order, so the
        /// composition is the row-vector <c>local = world × IBM_parent</c>: the transpose of the
        /// column-vector <c>local = inverse(world_parent) × world</c>, i.e. the same node matrix.
        /// </remarks>
        private static float[] BindPoseLocalMatrices(List<SkeletonJoint> joints)
        {
            var matrices = new float[joints.Count * 16];
            for (int i = 0; i < joints.Count; i++)
            {
                if (!Matrix4x4.Invert(ToMatrix(joints[i].InverseBindMatrix16), out var world))
                    throw new InvalidOperationException($"Joint '{joints[i].Name}': InverseBindMatrix16 is not invertible.");

                var local = world;
                var parent = joints[i].ParentIndex;
                if (parent >= 0) local = world * ToMatrix(joints[parent].InverseBindMatrix16);
                WriteMatrix(local, matrices, i * 16);
            }
            return matrices;
        }

        /// <summary>The DTO's 16 floats read as a row-vector matrix (their own element order).</summary>
        private static Matrix4x4 ToMatrix(float[] values, int offset = 0) => new(
            values[offset], values[offset + 1], values[offset + 2], values[offset + 3],
            values[offset + 4], values[offset + 5], values[offset + 6], values[offset + 7],
            values[offset + 8], values[offset + 9], values[offset + 10], values[offset + 11],
            values[offset + 12], values[offset + 13], values[offset + 14], values[offset + 15]);

        /// <summary>Writes a matrix back in the DTO's element order (glTF's column-major listing).</summary>
        private static void WriteMatrix(Matrix4x4 matrix, float[] destination, int offset)
        {
            destination[offset] = matrix.M11;
            destination[offset + 1] = matrix.M12;
            destination[offset + 2] = matrix.M13;
            destination[offset + 3] = matrix.M14;
            destination[offset + 4] = matrix.M21;
            destination[offset + 5] = matrix.M22;
            destination[offset + 6] = matrix.M23;
            destination[offset + 7] = matrix.M24;
            destination[offset + 8] = matrix.M31;
            destination[offset + 9] = matrix.M32;
            destination[offset + 10] = matrix.M33;
            destination[offset + 11] = matrix.M34;
            destination[offset + 12] = matrix.M41;
            destination[offset + 13] = matrix.M42;
            destination[offset + 14] = matrix.M43;
            destination[offset + 15] = matrix.M44;
        }

        private PrimitiveInfo AddMesh(MeshData mesh, int jointCount, int materialCount)
        {
            if (mesh.Positions.Length == 0 || mesh.Positions.Length % 3 != 0)
                throw new InvalidOperationException($"Mesh '{mesh.Name}': positions must be a non-empty multiple of 3.");
            int vertexCount = mesh.Positions.Length / 3;

            var attributes = new List<(string Semantic, int Accessor)>
            {
                ("POSITION", AddAccessor(FloatBytes(mesh.Positions), TargetArrayBuffer, ComponentFloat, "VEC3",
                    vertexCount, MinMax(mesh.Positions, vertexCount, min: true), MinMax(mesh.Positions, vertexCount, min: false))),
            };

            if (mesh.Normals.Length > 0)
            {
                RequireLength(mesh.Name, "normals", mesh.Normals.Length, vertexCount * 3);
                attributes.Add(("NORMAL", AddAccessor(FloatBytes(mesh.Normals), TargetArrayBuffer, ComponentFloat,
                    "VEC3", vertexCount, null, null)));
            }

            for (int channel = 0; channel < mesh.UvChannels.Length; channel++)
            {
                var uv = mesh.UvChannels[channel];
                RequireLength(mesh.Name, $"UV channel {channel}", uv.Length, vertexCount * 2);
                attributes.Add(($"TEXCOORD_{channel}", AddAccessor(FloatBytes(uv), TargetArrayBuffer, ComponentFloat,
                    "VEC2", vertexCount, null, null)));
            }

            // Skinning attributes are only meaningful with a skeleton to index into.
            bool skinned = jointCount > 0 && mesh.BoneIndices.Length > 0 && mesh.BoneWeights.Length > 0;
            if (skinned)
            {
                RequireLength(mesh.Name, "bone indices", mesh.BoneIndices.Length, vertexCount * 4);
                RequireLength(mesh.Name, "bone weights", mesh.BoneWeights.Length, vertexCount * 4);
                attributes.Add(("JOINTS_0", AddAccessor(UShortBytes(mesh.BoneIndices, jointCount), TargetArrayBuffer,
                    ComponentUShort, "VEC4", vertexCount, null, null)));
                attributes.Add(("WEIGHTS_0", AddAccessor(FloatBytes(mesh.BoneWeights), TargetArrayBuffer, ComponentFloat,
                    "VEC4", vertexCount, null, null)));
            }

            if (mesh.Indices.Length == 0 || mesh.Indices.Length % 3 != 0)
                throw new InvalidOperationException($"Mesh '{mesh.Name}': indices must be a non-empty triangle list.");
            int indices = AddAccessor(UIntBytes(mesh.Indices), TargetElementArrayBuffer, ComponentUInt, "SCALAR",
                mesh.Indices.Length, null, null);

            if (mesh.MaterialIndex < -1 || mesh.MaterialIndex >= materialCount)
                throw new InvalidOperationException($"Mesh '{mesh.Name}': material index {mesh.MaterialIndex} is out of range.");

            return new PrimitiveInfo(mesh.Name, attributes, indices, mesh.MaterialIndex, skinned);
        }

        /// <summary>Maps a source texture path to a deduplicated texture index; -1 = channel omitted.</summary>
        private int ResolveTexture(string? sourcePath)
        {
            if (string.IsNullOrEmpty(sourcePath)) return -1;
            if (!_textureUriMap.TryGetValue(sourcePath, out var uri) || string.IsNullOrEmpty(uri)) return -1;
            if (_textureIndexByUri.TryGetValue(uri, out var existing)) return existing;

            _imageUris.Add(uri);
            int index = _imageUris.Count - 1;
            _textureIndexByUri.Add(uri, index);
            return index;
        }

        private int AddAccessor(byte[] data, int? target, int componentType, string type, int count,
            float[]? min, float[]? max)
        {
            // Every bufferView starts 4 bytes in: keeps accessors aligned for any component type.
            while (_bin.Length % 4 != 0) _bin.WriteByte(0);
            _views.Add(new BufferViewInfo((int)_bin.Length, data.Length, target));
            _bin.Write(data, 0, data.Length);
            _accessors.Add(new AccessorInfo(_views.Count - 1, componentType, count, type, min, max));
            return _accessors.Count - 1;
        }

        private byte[] WriteJson()
        {
            using var stream = new MemoryStream();
            using (var w = new Utf8JsonWriter(stream))
            {
                w.WriteStartObject();

                w.WritePropertyName("asset");
                w.WriteStartObject();
                w.WriteString("version", "2.0");
                w.WriteString("generator", "bp-convert");
                w.WriteEndObject();

                if (_usesSpecularExtension)
                {
                    w.WritePropertyName("extensionsUsed");
                    w.WriteStartArray();
                    w.WriteStringValue("KHR_materials_specular");
                    w.WriteEndArray();
                }

                if (_joints.Count + _primitives.Count > 0)
                {
                    w.WriteNumber("scene", 0);
                    w.WritePropertyName("scenes");
                    w.WriteStartArray();
                    w.WriteStartObject();
                    w.WritePropertyName("nodes");
                    w.WriteStartArray();
                    foreach (var root in _jointRoots) w.WriteNumberValue(root);
                    for (int i = 0; i < _primitives.Count; i++) w.WriteNumberValue(_joints.Count + i);
                    w.WriteEndArray();
                    w.WriteEndObject();
                    w.WriteEndArray();

                    w.WritePropertyName("nodes");
                    w.WriteStartArray();
                    for (int i = 0; i < _joints.Count; i++)
                    {
                        w.WriteStartObject();
                        if (!string.IsNullOrEmpty(_joints[i].Name)) w.WriteString("name", _joints[i].Name);

                        if (_animatedJoints.Length > 0 && _animatedJoints[i])
                        {
                            // Animated: the bind-pose local transform as TRS, so the animation's
                            // channels can drive it (glTF forbids animating a `matrix` node).
                            var (translation, rotation, scale) = _jointBindTrs[i];
                            WriteFloats(w, "translation", new[] { translation.X, translation.Y, translation.Z });
                            WriteFloats(w, "rotation", new[] { rotation.X, rotation.Y, rotation.Z, rotation.W });
                            WriteFloats(w, "scale", new[] { scale.X, scale.Y, scale.Z });
                        }
                        else
                        {
                            // Bind-pose local transform (glTF column-major). See BindPoseLocalMatrices.
                            w.WritePropertyName("matrix");
                            w.WriteStartArray();
                            for (int k = 0; k < 16; k++) w.WriteNumberValue(_jointLocalMatrices[i * 16 + k]);
                            w.WriteEndArray();
                        }

                        if (_jointChildren[i].Count > 0)
                        {
                            w.WritePropertyName("children");
                            w.WriteStartArray();
                            foreach (var child in _jointChildren[i]) w.WriteNumberValue(child);
                            w.WriteEndArray();
                        }
                        w.WriteEndObject();
                    }
                    for (int i = 0; i < _primitives.Count; i++)
                    {
                        var primitive = _primitives[i];
                        w.WriteStartObject();
                        if (!string.IsNullOrEmpty(primitive.Name)) w.WriteString("name", primitive.Name);
                        w.WriteNumber("mesh", i);
                        if (primitive.Skinned) w.WriteNumber("skin", 0);
                        w.WriteEndObject();
                    }
                    w.WriteEndArray();
                }

                if (_primitives.Count > 0)
                {
                    w.WritePropertyName("meshes");
                    w.WriteStartArray();
                    for (int i = 0; i < _primitives.Count; i++)
                    {
                        var primitive = _primitives[i];
                        w.WriteStartObject();
                        if (!string.IsNullOrEmpty(primitive.Name)) w.WriteString("name", primitive.Name);
                        w.WritePropertyName("primitives");
                        w.WriteStartArray();
                        w.WriteStartObject();
                        w.WritePropertyName("attributes");
                        w.WriteStartObject();
                        foreach (var (semantic, accessor) in primitive.Attributes) w.WriteNumber(semantic, accessor);
                        w.WriteEndObject();
                        w.WriteNumber("indices", primitive.IndicesAccessor);
                        if (primitive.MaterialIndex >= 0) w.WriteNumber("material", primitive.MaterialIndex);
                        w.WriteEndObject();
                        w.WriteEndArray();
                        w.WriteEndObject();
                    }
                    w.WriteEndArray();
                }

                if (_materials.Count > 0)
                {
                    w.WritePropertyName("materials");
                    w.WriteStartArray();
                    for (int i = 0; i < _materials.Count; i++) WriteMaterial(w, _materials[i], _materialTextures[i]);
                    w.WriteEndArray();
                }

                if (_joints.Count > 0)
                {
                    w.WritePropertyName("skins");
                    w.WriteStartArray();
                    w.WriteStartObject();
                    if (!string.IsNullOrEmpty(_skinName)) w.WriteString("name", _skinName);
                    w.WritePropertyName("joints");
                    w.WriteStartArray();
                    for (int i = 0; i < _joints.Count; i++) w.WriteNumberValue(i);
                    w.WriteEndArray();
                    w.WriteNumber("inverseBindMatrices", _ibmAccessor);
                    w.WriteEndObject();
                    w.WriteEndArray();
                }

                if (_animations.Count > 0)
                {
                    w.WritePropertyName("animations");
                    w.WriteStartArray();
                    foreach (var animation in _animations)
                    {
                        w.WriteStartObject();
                        if (!string.IsNullOrEmpty(animation.Name)) w.WriteString("name", animation.Name);

                        w.WritePropertyName("channels");
                        w.WriteStartArray();
                        foreach (var channel in animation.Channels)
                        {
                            w.WriteStartObject();
                            w.WriteNumber("sampler", channel.Sampler);
                            w.WritePropertyName("target");
                            w.WriteStartObject();
                            w.WriteNumber("node", channel.Node);
                            w.WriteString("path", channel.Path);
                            w.WriteEndObject();
                            w.WriteEndObject();
                        }
                        w.WriteEndArray();

                        w.WritePropertyName("samplers");
                        w.WriteStartArray();
                        foreach (var sampler in animation.Samplers)
                        {
                            w.WriteStartObject();
                            w.WriteNumber("input", sampler.Input);
                            w.WriteNumber("output", sampler.Output);
                            w.WriteString("interpolation", "LINEAR");
                            w.WriteEndObject();
                        }
                        w.WriteEndArray();

                        w.WriteEndObject();
                    }
                    w.WriteEndArray();
                }

                if (_imageUris.Count > 0)
                {
                    w.WritePropertyName("images");
                    w.WriteStartArray();
                    foreach (var uri in _imageUris)
                    {
                        w.WriteStartObject();
                        w.WriteString("uri", uri);
                        w.WriteEndObject();
                    }
                    w.WriteEndArray();

                    w.WritePropertyName("textures");
                    w.WriteStartArray();
                    for (int i = 0; i < _imageUris.Count; i++)
                    {
                        w.WriteStartObject();
                        w.WriteNumber("source", i);
                        w.WriteEndObject();
                    }
                    w.WriteEndArray();
                }

                if (_accessors.Count > 0)
                {
                    w.WritePropertyName("accessors");
                    w.WriteStartArray();
                    foreach (var accessor in _accessors)
                    {
                        w.WriteStartObject();
                        w.WriteNumber("bufferView", accessor.View);
                        w.WriteNumber("componentType", accessor.ComponentType);
                        w.WriteNumber("count", accessor.Count);
                        w.WriteString("type", accessor.Type);
                        if (accessor.Min != null) WriteFloats(w, "min", accessor.Min);
                        if (accessor.Max != null) WriteFloats(w, "max", accessor.Max);
                        w.WriteEndObject();
                    }
                    w.WriteEndArray();

                    w.WritePropertyName("bufferViews");
                    w.WriteStartArray();
                    foreach (var view in _views)
                    {
                        w.WriteStartObject();
                        w.WriteNumber("buffer", 0);
                        w.WriteNumber("byteOffset", view.ByteOffset);
                        w.WriteNumber("byteLength", view.ByteLength);
                        if (view.Target.HasValue) w.WriteNumber("target", view.Target.Value);
                        w.WriteEndObject();
                    }
                    w.WriteEndArray();

                    w.WritePropertyName("buffers");
                    w.WriteStartArray();
                    w.WriteStartObject();
                    w.WriteNumber("byteLength", _bin.Length);
                    w.WriteEndObject();
                    w.WriteEndArray();
                }

                w.WriteEndObject();
            }

            return stream.ToArray();
        }

        private static void WriteMaterial(Utf8JsonWriter w, MaterialData material, MaterialTextures textures)
        {
            w.WriteStartObject();
            if (!string.IsNullOrEmpty(material.Name)) w.WriteString("name", material.Name);

            w.WritePropertyName("pbrMetallicRoughness");
            w.WriteStartObject();
            if (material.BaseColorFactor.Length > 0) WriteFloats(w, "baseColorFactor", Rgba(material.BaseColorFactor));
            if (textures.BaseColor >= 0) WriteTextureRef(w, "baseColorTexture", textures.BaseColor);
            // glTF's default metallicFactor is 1.0 (fully metallic) and the BluePoint pipeline has
            // no metallic input, so every material is dielectric until an M1 material pass exists.
            // Roughness keeps its 1.0 default (the accepted approximation for now).
            w.WriteNumber("metallicFactor", 0);
            w.WriteEndObject();

            if (material.EmissiveFactor.Length >= 3) WriteFloats(w, "emissiveFactor", material.EmissiveFactor[..3]);
            else if (material.EmissiveFactor.Length != 0)
                throw new InvalidOperationException($"Material '{material.Name}': emissive factor must have 3 components.");

            // The veins carrier (S6 D9): a standard emissiveTexture slot. The factor above stays as
            // the caller set it (black for the corpse), so a conformant viewer sees no new glow.
            if (textures.Emissive >= 0) WriteTextureRef(w, "emissiveTexture", textures.Emissive);

            if (textures.Normal >= 0) WriteTextureRef(w, "normalTexture", textures.Normal);

            if (textures.Specular >= 0)
            {
                w.WritePropertyName("extensions");
                w.WriteStartObject();
                w.WritePropertyName("KHR_materials_specular");
                w.WriteStartObject();
                WriteTextureRef(w, "specularTexture", textures.Specular);
                w.WriteEndObject();
                w.WriteEndObject();
            }

            w.WriteEndObject();
        }

        private static void WriteTextureRef(Utf8JsonWriter w, string propertyName, int textureIndex)
        {
            w.WritePropertyName(propertyName);
            w.WriteStartObject();
            w.WriteNumber("index", textureIndex);
            w.WriteEndObject();
        }

        private static void WriteFloats(Utf8JsonWriter w, string propertyName, float[] values)
        {
            w.WritePropertyName(propertyName);
            w.WriteStartArray();
            foreach (var value in values) w.WriteNumberValue(value);
            w.WriteEndArray();
        }

        /// <summary>glTF headers and chunk table, with 4-byte aligned chunks (JSON padded with 0x20, BIN with 0x00).</summary>
        private static byte[] Pack(byte[] json, byte[] bin)
        {
            int jsonPadded = Align4(json.Length);
            int binPadded = Align4(bin.Length);
            int total = 12 + 8 + jsonPadded + (bin.Length > 0 ? 8 + binPadded : 0);
            var glb = new byte[total];

            BinaryPrimitives.WriteUInt32LittleEndian(glb, GlbMagic);
            BinaryPrimitives.WriteUInt32LittleEndian(glb.AsSpan(4), 2);
            BinaryPrimitives.WriteUInt32LittleEndian(glb.AsSpan(8), (uint)total);

            int offset = 12;
            WriteChunk(glb, ref offset, JsonChunkType, json, 0x20);
            if (bin.Length > 0) WriteChunk(glb, ref offset, BinChunkType, bin, 0x00);
            return glb;
        }

        private static void WriteChunk(byte[] glb, ref int offset, uint chunkType, byte[] payload, byte pad)
        {
            int padded = Align4(payload.Length);
            BinaryPrimitives.WriteUInt32LittleEndian(glb.AsSpan(offset), (uint)padded);
            BinaryPrimitives.WriteUInt32LittleEndian(glb.AsSpan(offset + 4), chunkType);
            payload.CopyTo(glb.AsSpan(offset + 8));
            for (int i = payload.Length; i < padded; i++) glb[offset + 8 + i] = pad;
            offset += 8 + padded;
        }

        private static int Align4(int length) => (length + 3) & ~3;

        private static byte[] FloatBytes(float[] values)
        {
            var bytes = new byte[values.Length * 4];
            for (int i = 0; i < values.Length; i++) BinaryPrimitives.WriteSingleLittleEndian(bytes.AsSpan(i * 4), values[i]);
            return bytes;
        }

        private static byte[] UIntBytes(uint[] values)
        {
            var bytes = new byte[values.Length * 4];
            for (int i = 0; i < values.Length; i++) BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(i * 4), values[i]);
            return bytes;
        }

        private static byte[] UShortBytes(int[] values, int jointCount)
        {
            var bytes = new byte[values.Length * 2];
            for (int i = 0; i < values.Length; i++)
            {
                if (values[i] < 0 || values[i] >= jointCount)
                    throw new InvalidOperationException($"Bone index {values[i]} is out of range (joints: {jointCount}).");

                BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(i * 2), (ushort)values[i]);
            }
            return bytes;
        }

        private static float[] MinMax(float[] values, int vertexCount, bool min)
        {
            var result = new float[3];
            for (int i = 0; i < 3; i++) result[i] = values[i];
            for (int vertex = 1; vertex < vertexCount; vertex++)
            {
                for (int i = 0; i < 3; i++)
                {
                    float value = values[vertex * 3 + i];
                    if (min ? value < result[i] : value > result[i]) result[i] = value;
                }
            }
            return result;
        }

        private static void RequireLength(string meshName, string data, int actual, int expected)
        {
            if (actual != expected)
                throw new InvalidOperationException($"Mesh '{meshName}': {data} has {actual} values, expected {expected}.");
        }

        private static float[] Rgba(float[] factor) => factor.Length switch
        {
            >= 4 => factor[..4],
            3 => new[] { factor[0], factor[1], factor[2], 1f },
            _ => throw new InvalidOperationException("Base color factor must have 3 or 4 components."),
        };

        private readonly record struct BufferViewInfo(int ByteOffset, int ByteLength, int? Target);
        private readonly record struct AccessorInfo(int View, int ComponentType, int Count, string Type, float[]? Min, float[]? Max);
        private sealed record PrimitiveInfo(string Name, List<(string Semantic, int Accessor)> Attributes, int IndicesAccessor,
            int MaterialIndex, bool Skinned);
        private readonly record struct MaterialTextures(int BaseColor, int Normal, int Specular, int Emissive);
        private sealed record AnimationInfo(string Name, List<ChannelInfo> Channels, List<SamplerInfo> Samplers);
        private readonly record struct ChannelInfo(int Node, string Path, int Sampler);
        private readonly record struct SamplerInfo(int Input, int Output);
    }
}

/// <summary>Result of <see cref="GlbWriter.Write"/>: absolute .glb path and its size in bytes.</summary>
public sealed record GlbWriteResult(string GlbPath, long Bytes);
