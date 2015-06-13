#include <iostream>
#include <vector>
#include <algorithm>
#include <stdint.h>
#include <string>
#include <sstream>
#include <zim/endian.h>
#include <zim/file.h>
#include <zim/fileiterator.h>
#include <zim/article.h>
#include <zim/blob.h>

template <typename T>
void writeLittleEndian(std::ostream& out, T const& value)
{
  char buffer[sizeof(T)];
  zim::toLittleEndian(value, buffer);
  out.write(buffer, sizeof(T));
}

template <typename T>
T readLittleEndian(std::istream& in)
{
  char buffer[sizeof(T)];
  in.read(buffer, sizeof(T));
  return zim::fromLittleEndian<T>(reinterpret_cast<const T*>(buffer + 0));
}

struct GeoPoint
{
    uint32_t latitude;
    uint32_t longitude;
    uint32_t axisValue(unsigned axis) const
    {
      return axis == 0 ? latitude : longitude;
    }
    uint32_t& axisValue(unsigned axis)
    {
      return axis == 0 ? latitude : longitude;
    }
    bool valid() const { return latitude != 0 || longitude != 0; }
    bool operator<=(GeoPoint const& _other) const
    {
      return latitude <= _other.latitude && longitude <= _other.longitude;
    }
};

struct ArticlePoint : GeoPoint
{
    zim::size_type index;

    ArticlePoint(): index(std::numeric_limits<zim::size_type>::max()) {}

    friend std::ostream& operator<<(std::ostream& out, ArticlePoint const& p)
    {
      writeLittleEndian(out, p.latitude);
      writeLittleEndian(out, p.longitude);
      writeLittleEndian(out, p.index);
      return out;
    }
    friend std::istream& operator>>(std::istream& in, ArticlePoint& p)
    {
      p.latitude = readLittleEndian<uint32_t>(in);
      p.longitude = readLittleEndian<uint32_t>(in);
      p.index = readLittleEndian<zim::size_type>(in);
      return in;
    }
};

template <unsigned axis>
struct AxisComparator
{
    bool operator()(GeoPoint const& a, GeoPoint const& b) const
    {
      return a.axisValue(axis) < b.axisValue(axis);
    }
};

typedef std::vector<ArticlePoint>::iterator ArticlePointIterator;

int32_t integerRangeToMicroDegrees(uint32_t coordIntRange)
{
  return int32_t((uint64_t(coordIntRange) * 360000000) >> 32) - 180000000;
}

uint32_t microDegreesToIntegerRange(int32_t coordMicroDegrees)
{
  // input range: -180 000 000 to +180 000 000
  // output range: 0 to 4 294 967 295
  return uint32_t((uint64_t(coordMicroDegrees + 180000000) << 32) / 360000000);
}

std::string integerRangeToString(uint32_t value, unsigned axis)
{
  int32_t microDegs = integerRangeToMicroDegrees(value);
  if (axis == 0)
    microDegs /= 2;
  std::ostringstream buf;
  buf << (double(microDegs) / 1000000);
  return buf.str();
}

void printRange(std::ostream& out, GeoPoint const& min, GeoPoint const& max)
{
  std::cout << "Searching in " <<
    integerRangeToString(min.latitude, 0) << ", " <<
    integerRangeToString(min.longitude, 1) << " - " <<
    integerRangeToString(max.latitude, 0) << ", " <<
    integerRangeToString(max.longitude, 1) << std::endl;
}

void writeQuadtree(std::ostream& out, ArticlePointIterator begin, ArticlePointIterator end, unsigned depth = 0)
{
  if (end - begin < 10)
  {
    writeLittleEndian(out, uint32_t(end - begin));
    for (; begin != end; ++begin)
      out << *begin;
  }
  else
  {
    if (depth % 2)
      std::sort(begin, end, AxisComparator<1>());
    else
      std::sort(begin, end, AxisComparator<0>());
    ArticlePointIterator median = begin + (end - begin) / 2;
    uint32_t medianValue = median->axisValue(depth % 2);
    if (medianValue < 10)
    {
      // We cannot have such a median value, because this would make this node a leaf node.
      std::cerr << "Median value of less than 10 encountered - too many small coordinates. " << std::endl;
      std::cerr << "Will throw away some points." << std::endl;
      writeQuadtree(out, begin + 1, end, depth);
      return;
    }
    // Decrement the median as long as the value is the same.
    while (median > begin && (median - 1)->axisValue(depth % 2) == medianValue)
      --median;
    writeLittleEndian(out, medianValue);
    std::ostream::pos_type offsetPos = out.tellp();
    writeLittleEndian(out, uint32_t(0)); // will be overwritten later

    writeQuadtree(out, begin, median, depth + 1);

    uint32_t greaterPos = uint32_t(out.tellp());
    out.seekp(offsetPos);
    writeLittleEndian(out, greaterPos);
    out.seekp(0, std::ios_base::end);

    writeQuadtree(out, median, end, depth + 1);
  }
}

void searchRange(std::istream& in, GeoPoint min, GeoPoint max, unsigned depth, std::vector<ArticlePoint>& points)
{
  uint32_t value = readLittleEndian<uint32_t>(in);
  if (value < 10)
  {
    std::cout << "Descended to depth " << depth << std::endl;
    for (uint32_t i = 0; i < value; ++i)
    {
      ArticlePoint point;
      in >> point;
      if (min <= point && point <= max)
        points.push_back(point);
    }
    return;
  }
  uint32_t greaterPos = readLittleEndian<uint32_t>(in);
  if (min.axisValue(depth % 2) < value)
  {
    GeoPoint maxCopy = max;
    maxCopy.axisValue(depth % 2) = std::min(maxCopy.axisValue(depth % 2), value);
    searchRange(in, min, maxCopy, depth + 1, points);
  }
  if (value <= max.axisValue(depth % 2))
  {
    GeoPoint minCopy = min;
    minCopy.axisValue(depth % 2) = std::max(minCopy.axisValue(depth % 2), value);
    in.seekg(greaterPos);
    searchRange(in, minCopy, max, depth + 1, points);
  }
}

int32_t parseCoordinateMicroDegrees(char const*& text)
{
  bool negative = false;
  int32_t value = 0;
  if (*text == '-')
  {
    negative = true;
    ++text;
  }
  unsigned beyondDecimal = 0;
  for (; *text == '.' || ('0' <= *text && *text <= '9'); ++text)
  {
    if (*text == '.')
    {
      if (beyondDecimal > 0)
        break;
      else
        beyondDecimal = 1;
    }
    else
    {
      value = value * 10 + (*text - '0');
      if (beyondDecimal > 0)
        ++beyondDecimal;
    }
  }
  if (beyondDecimal == 0)
    beyondDecimal = 1;
  for (; beyondDecimal < 7; ++beyondDecimal)
    value *= 10;
  return negative ? -value : value;
}

ArticlePoint parsePoint(zim::size_type index, char const* coordinates)
{
  int32_t latitudeMicroDegrees = parseCoordinateMicroDegrees(coordinates);
  if (!coordinates || *coordinates != ';')
    return ArticlePoint();
  coordinates++;
  int32_t longitudeMicroDegrees = parseCoordinateMicroDegrees(coordinates);
  if (!coordinates)
    return ArticlePoint();

  ArticlePoint p;
  p.index = index;
  p.latitude = microDegreesToIntegerRange(latitudeMicroDegrees * 2);
  p.longitude = microDegreesToIntegerRange(longitudeMicroDegrees);
  return p;
}

void encodePoints(std::string const& filename)
{
  static char const* metaTag = "<meta name=\"geo.position\" content=\"";
  std::vector<ArticlePoint> points;

  zim::File zimfile(filename);
  for (zim::File::const_iterator it = zimfile.begin(); it != zimfile.end(); ++it)
  {
    zim::Article const& article = *it;
    if (article.isRedirect() || article.isDeleted())
      continue;
    zim::Blob data = article.getData();
    char const* tag = std::search(data.data(), data.end(), metaTag, metaTag + std::strlen(metaTag));
    if (tag != data.end())
    {
      ArticlePoint p = parsePoint(article.getIndex(), tag + std::strlen(metaTag));
      if (p.valid())
        points.push_back(p);
    }
  }
  writeQuadtree(std::cout, points.begin(), points.end());
}

void search(char const* latMin, char const* lonMin, char const* latMax, char const* lonMax)
{
  GeoPoint min;
  GeoPoint max;
  min.latitude = microDegreesToIntegerRange(parseCoordinateMicroDegrees(latMin) * 2);
  min.longitude = microDegreesToIntegerRange(parseCoordinateMicroDegrees(lonMin));
  max.latitude = microDegreesToIntegerRange(parseCoordinateMicroDegrees(latMax) * 2);
  max.longitude = microDegreesToIntegerRange(parseCoordinateMicroDegrees(lonMax));
  if (min.latitude > max.latitude)
    std::swap(min.latitude, max.latitude);
  if (min.longitude > max.longitude)
    std::swap(min.longitude, max.longitude);

  printRange(std::cout, min, max);

  std::vector<ArticlePoint> points;
  searchRange(std::cin, min, max, 0, points);
  for (std::vector<ArticlePoint>::const_iterator it = points.begin(); it != points.end(); ++it)
    std::cout <<
      integerRangeToString(it->latitude, 0) << ", " <<
      integerRangeToString(it->longitude, 1) << ": " <<
      it->index << std::endl;
}

int main(int argc, char* argv[])
{
  if (argc == 2)
    encodePoints(argv[1]);
  else if (argc == 5)
    search(argv[1], argv[2], argv[3], argv[4]);
  else
    std::cout << "Invalid arguments" << std::endl;
}
