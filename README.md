# Tiny Renderer

Unreal Engine で利用可能な、単一の StaticMesh を RenderTarget に描画することだけを考えた小さくて軽量なレンダラプラグインです。

## 使い方
[この記事](https://strv.dev/blog/unrealengine--lets-implement-a-single-mesh-renderer-3/#%E3%83%97%E3%83%A9%E3%82%B0%E3%82%A4%E3%83%B3%E3%81%AE%E5%85%AC%E9%96%8B) を参照してください。

## 検証環境
- UE 5.3.2
- Win64

## サポートしている機能
- Opaque なマテリアルが適用された StaticMesh を RenderTarget に描画

## サポートしない機能
- 多数のメッシュからなるシーンの描画
- 複雑な照明環境の描画
- エフェクトやポストプロセスの適用